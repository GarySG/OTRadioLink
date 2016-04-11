/*
The OpenTRV project licenses this file to you
under the Apache Licence, Version 2.0 (the "Licence");
you may not use this file except in compliance
with the Licence. You may obtain a copy of the Licence at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the Licence is distributed on an
"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, either express or implied. See the Licence for the
specific language governing permissions and limitations
under the Licence.

Author(s) / Copyright (s): Deniz Erbilgin 2015-2016
                           Damon Hart-Davis 2015
*/

#include "OTSIM900Link_OTSIM900Link.h"

namespace OTSIM900Link
{

/**
 * @brief    Constructor. Initializes softSerial and sets PWR_PIN
 * @param    pwrPin        SIM900 power on/off pin
 * @param    rxPin        Rx pin for software serial
 * @param    txPin        Tx pin for software serial
 *
 * Cannot do anything with side-effects,
 * as may be called before run-time fully initialised!
 */
OTSIM900Link::OTSIM900Link(uint8_t hardPwrPin, uint8_t pwrPin, uint8_t rxPin, uint8_t txPin)
  : HARD_PWR_PIN(hardPwrPin), PWR_PIN(pwrPin), softSerial(rxPin, txPin)
{
//  pinMode(PWR_PIN, OUTPUT); // Can't do here since this constructor may be static/global.
  bAvailable = false;
  bPowered = false;
  config = NULL;
  state = IDLE;
  memset(txQueue, 0, sizeof(txQueue));
  txMsgLen = 0;
  txMessageQueue = 0;
}

/**
 * @brief     Assigns OTSIM900LinkConfig config. Must be called before begin()
 * @retval    returns true if assigned or false if config is NULL
 */
bool OTSIM900Link::_doconfig()
{
    if (channelConfig->config == NULL) return false;
    else {
        config = (const OTSIM900LinkConfig_t *) channelConfig->config;
        return true;
    }
}

/**
 * @brief    Starts software serial, checks for module and inits state
 */
bool OTSIM900Link::begin()
{
    pinMode(PWR_PIN, OUTPUT);
    fastDigitalWrite(PWR_PIN, LOW);
    softSerial.begin(baud);
    state = GET_STATE;
    return true;
}

/**
 * @brief    close UDP connection and power down SIM module
 */
bool OTSIM900Link::end()
{
    closeUDP();
    powerOff();
    return false;
}

/**
 * @brief    Sends message. Will shut UDP and attempt to resend if sendUDP fails
 * @todo    clean this up
 * @param    buf        pointer to buffer to send
 * @param    buflen    length of buffer to send
 * @param    channel    ignored
 * @param    Txpower    ignored
 * @retval    returns true if send process inited
 * @note    requires calling of poll() to check if message sent successfully
 */
bool OTSIM900Link::sendRaw(const uint8_t *buf, uint8_t buflen, int8_t , TXpower , bool)
{
    bool bSent = false;

#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush(F("Send Raw"));
#endif // OTSIM900LINK_DEBUG
    bSent = sendUDP((const char *)buf, buflen);
    return bSent;
}

/**
 * @brief    Puts message in queue to send on wakeup
 * @param    buf        pointer to buffer to send
 * @param    buflen    length of buffer to send
 * @param    channel    ignored
 * @param    Txpower    ignored
 * @retval    returns true if send process inited
 * @note    requires calling of poll() to check if message sent successfully
 */
bool OTSIM900Link::queueToSend(const uint8_t *buf, uint8_t buflen, int8_t , TXpower )
{
    if ((buf == NULL) || (buflen > sizeof(txQueue)) || (txMessageQueue >= maxTxQueueLength)) return false;    // TODO check logic and sort out maxTXMsgLen problem
    // Increment message queue
    txMessageQueue++;
    // copy into queue here?
    memcpy(txQueue, buf, buflen);
    txMsgLen = buflen;
    return true;
}

/**
 * @brief    Polling routine steps through 4 stage state machine
 * @todo    test 2 stage state machine
 *             add in other stages
 *             allow for sending multiple messages in one session
 */
void OTSIM900Link::poll()
{
    switch (state) {
    case GET_STATE:
        // Check SIM900 is present and can be talked to.
        // Panics on failure.
        memset(txQueue, 0, sizeof(txQueue));
        txMsgLen = 0;
        txMessageQueue = 0;
        if(!getInitState()) {
            state = SIM900_FOUND;
            delay(5000); // TODO get rid of this line
            powerOn();
        }
//        else panic();
        break;
    case SIM900_FOUND:
        // Set pin if required.
        // Panics on failure to avoid locking SIM.
        if(checkPIN()) state = WAIT_FOR_REGISTRATION;
        else if(!setPIN()) /*panic();*/{} // TODOmake sure setPin returns true or false
        break;
    case WAIT_FOR_REGISTRATION:
        // Wait for registration to GSM network.
        // Stuck in this state until success.
        if(isRegistered()) state = SET_APN;
        break;
    case SET_APN:
        // Attempt to set the APN.
        // Stuck in this state until success.
        if(!setAPN()) state = START_GPRS;
        break;
    case START_GPRS:
        // Start GPRS context.
        // TODO: Add retries, Option to shut GPRS here (probably needs a new state)
        if(!startGPRS()) state = IDLE;
//        else panic(); // If startGPRS() ever returns an error here then something has probably gone very wrong
        break;
    case IDLE:
        // Waiting for message.
        // If message is queued, go to WAIT_FOR_UDP
        if (txMessageQueue > 0) {
#ifdef OTSIM900LINK_DEBUG
            getSignalStrength(); // Helps with debugging but otherwise useless;
#endif // OTSIM900LINK_DEBUG
            state = WAIT_FOR_UDP; // TODO-748
        }
        break;
// TODO: This needs a new state.
// Note: This step will need to be made optional as part of TODO-748
//    case START_GPRS:
//        // Start GPRS context.
//        // If this fails, SIM900 is probably stuck in dead end PDP_DEACT state
//        // so powercycle SIM900 and try again. Probably worth adding retries.
//        if(startGPRS()) state = WAIT_FOR_UDP; // TODO make sure startGPRS returns correctly
//
//        break;
    case WAIT_FOR_UDP:
        // Start a UDP context and send when ready
        // If GPRS not active, start it.
        // Go to RESTART_CONNECTION on fail
    {
        uint8_t udpState = isOpenUDP();
        if(udpState == 1) state = WAIT_FOR_UDP;
        else if (udpState == 0) state = START_GPRS;
        else if (udpState == 2) state = GET_STATE;
    }
        break;
    case SENDING:
        // Attempt to send a message
        // Once done, decrement number of messages in queue and return to IDLE
        if (txMessageQueue > 0) {
            // TODO logic to check if send attempt successful
            sendRaw(txQueue, txMsgLen); /// @note can't use strlen with encrypted/binary packets
        }
        if(!(--txMessageQueue)) state = IDLE;
        break;
    default:
        break;
    }
}

/**
 * @brief    open UDP connection to input ip address
 * @todo    Check for successful open
 *             find better way of writing this
 * @param    array containing server IP
 * @retval    returns true if UDP opened
 * @note    is it necessary to check if UDP open?
 */
bool OTSIM900Link::openUDP()
{
    char data[MAX_SIM900_RESPONSE_CHARS];
    memset(data, 0, sizeof(data));
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush(F("Open UDP"));
#endif // OTSIM900LINK_DEBUG
    print(AT_START);
    print(AT_START_UDP);
    print("=\"UDP\",");
    print('\"');
    print(config->UDP_Address);
    print("\",\"");
    print(config->UDP_Port);
    print('\"');
    print(AT_END);

    // Implement check here
    timedBlockingRead(data, sizeof(data));
    // response stuff
    uint8_t dataCutLength = 0;
    getResponse(dataCutLength, data, sizeof(data), 0x0A);

    return true;
}

/**
 * @brief    close UDP connection
 * @todo    implement checks
 * @retval    returns true if UDP closed
 * @note    check UDP open
 */
bool OTSIM900Link::closeUDP()
{
    print(AT_START);
    print(AT_CLOSE_UDP);
    print(AT_END);
    return false;
}

/**
 * @brief    send UDP frame
 * @todo    add check for successful send
 *             split this into init sending and write message
 *             How will size of message be found/passed?
 * @param    pointer to array containing frame to send
 * @param    length of frame
 * @retval    returns true if send successful
 * @note    check UDP open
 */
bool OTSIM900Link::sendUDP(const char *frame, uint8_t length)
{
    // TODO this bit will be initSendUDP
//    OTV0P2BASE::serialPrintAndFlush(OTV0P2BASE::getSecondsLT());

    print(AT_START);
    print(AT_SEND_UDP);
    print('=');
    print(length);
    print(AT_END);

    // TODO flushUntil may be replaced with isr routine
//     '>' indicates module is ready for UDP frame
    if (flushUntil('>')) {
        // TODO this bit will remain in this
        write(frame, length);
//        delay(200);
        return true;    // add check here
    } else return false;
}

/**
 * @brief    Reads a single character from softSerial
 * @retval    returns character read, or 0 if no data to read
 */
uint8_t OTSIM900Link::read()
{
    uint8_t data;
    data = softSerial.read();
    return data;
}

/**
 * @brief    Enter blocking read. Fills buffer or times out after 100 ms
 * @param    data    data buffer to write to
 * @param    length    length of data buffer
 * @retval    number of characters received before time out
 */
uint8_t OTSIM900Link::timedBlockingRead(char *data, uint8_t length)
{;
  // clear buffer, get time and init i to 0
  memset(data, 0, length);
  uint8_t i = 0;

  i = softSerial.read((uint8_t *)data, length);

#if 0 && defined(OTSIM900LINK_DEBUG)
  OTV0P2BASE::serialPrintAndFlush(F("\n--Buffer Length: "));
  OTV0P2BASE::serialPrintAndFlush(i);
  OTV0P2BASE::serialPrintlnAndFlush();
#endif // OTSIM900LINK_DEBUG
  return i;
}

/**
 * @brief   blocks process until terminatingChar received
 * @param    terminatingChar        character to block until
 * @retval    returns true if character found, or false on 1000ms timeout
 */
bool OTSIM900Link::flushUntil(uint8_t _terminatingChar)
{
    const uint8_t terminatingChar = _terminatingChar;

#ifdef OTSIM900LINK_DEBUG
//    OTV0P2BASE::serialPrintAndFlush(F("- Flush: "));
#endif // OTSIM900LINK_DEBUG

    const uint8_t endTime = OTV0P2BASE::getSecondsLT() + flushTimeOut;
    while (OTV0P2BASE::getSecondsLT() <= endTime) { // FIXME Replace this logic
        const uint8_t c = read();
        if (c == terminatingChar) return true;
    }
#if 0 && defined(OTSIM900LINK_DEBUG)
  OTV0P2BASE::serialPrintlnAndFlush(F(" Timeout"));
  OTV0P2BASE::serialPrintAndFlush(OTV0P2BASE::getSecondsLT());
#endif // OTSIM900LINK_DEBUG

  return false;
}
/**
 * @brief    Writes an array to software serial
 * @param    data    data buffer to write from
 * @param    length    length of data buffer
 */
void OTSIM900Link::write(const char *data, uint8_t length)
{
    softSerial.write(data, length);
}

/**
 * @brief    Writes a character to software serial
 * @param    data    character to write
 */
void OTSIM900Link::print(char data)
{
    softSerial.print(data);
}

/**
 * @brief  Writes a character to software serial
 * @param data  character to write
 */
void OTSIM900Link::print(const uint8_t value)
{
  softSerial.printNum(value);    // FIXME
}

void OTSIM900Link::print(const char *string)
{
    softSerial.print(string);
}

/**
 * @brief    Copies string from EEPROM and prints to softSerial
 * @fixme    Potential for infinite loop
 * @param    pointer to eeprom location string is stored in
 */
void OTSIM900Link::print(const void *src)
{
    char c = 0xff;    // to avoid exiting the while loop without \0 getting written
    const uint8_t *ptr = (const uint8_t *) src;
    // loop through and print each value
    while (1) {
        c = config->get(ptr);
        if (c == '\0') return;
        print(c);
        ptr++;
    }
}

/**
 * @brief   Checks module ID
 * @todo    Implement check?
 * @param   name    pointer to array to compare name with
 * @param   length    length of array name
 * @retval  returns true if ID recovered successfully
 */
bool OTSIM900Link::checkModule()
 {
  char data[min(32, MAX_SIM900_RESPONSE_CHARS)];
  print(AT_START);
  print(AT_GET_MODULE);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));
#ifdef OTSIM900LINK_DEBUG
  OTV0P2BASE::serialPrintAndFlush(data);
  OTV0P2BASE::serialPrintlnAndFlush();
#endif // OTSIM900LINK_DEBUG
  return true;
}

/**
 * @brief   Checks connected network
 * @todo    implement check
 * @param   buffer    pointer to array to store network name in
 * @param   length    length of buffer
 * @param   returns true if connected to network
 */
bool OTSIM900Link::checkNetwork()
{
  char data[MAX_SIM900_RESPONSE_CHARS];
  print(AT_START);
  print(AT_NETWORK);
  print(AT_QUERY);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));
  return true;
}

/**
 * @brief   check if module connected and registered (GSM and GPRS)
 * @todo    implement check
 *          are two registration checks needed?
 * @retval  true if registered
 */
bool OTSIM900Link::isRegistered()
{
//  Check the GSM registration via AT commands ( "AT+CREG?" returns "+CREG:x,1" or "+CREG:x,5"; where "x" is 0, 1 or 2).
//  Check the GPRS registration via AT commands ("AT+CGATT?" returns "+CGATT:1" and "AT+CGREG?" returns "+CGREG:x,1" or "+CGREG:x,5"; where "x" is 0, 1 or 2). 

  char data[MAX_SIM900_RESPONSE_CHARS];

#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush(F("WF Reg"));
#endif // OTSIM900LINK_DEBUG

  print(AT_START);
  print(AT_REGISTRATION);
  print(AT_QUERY);
  print(AT_END);

  timedBlockingRead(data, sizeof(data));

  // response stuff
  const char *dataCut;
  uint8_t dataCutLength = 0;
  dataCut = getResponse(dataCutLength, data, sizeof(data), ' '); // first ' ' appears right before useful part of message

  if (dataCut[2] == '1' || dataCut[2] == '5' ) return true;    // expected response '1' or '5'
  else {
#if 0 && defined(OTSIM900LINK_DEBUG)
      getSignalStrength(); // Really only for debug
#endif // OTSIM900LINK_DEBUG
      return false;
  }
}

/**
 * @brief   Set Access Point Name and start task
 * @param   APN        pointer to access point name
 * @param   length    length of access point name
 * @retval  0 if APN set
 *          -1 if failed to set
 */
uint8_t OTSIM900Link::setAPN()
{
  char data[MAX_SIM900_RESPONSE_CHARS]; // FIXME: was 96: that's a LOT of stack!
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush(F("APN"));
#endif // OTSIM900LINK_DEBUG
  print(AT_START);
  print(AT_SET_APN);
  print(AT_SET);
  print(config->APN);
  print(AT_END);

  timedBlockingRead(data, sizeof(data));

  // response stuff
  const char *dataCut;
  uint8_t dataCutLength = 0;
  dataCut = getResponse(dataCutLength, data, sizeof(data), 0x0A);
#if 0 && defined(OTSIM900LINK_DEBUG)
  OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG

  if (*dataCut == 'O') return 0;    // expected response 'OK'
  else return -1;
}

/**
 * @brief   Start GPRS connection
 * @retval  0 if connected
 *          -1 if failed
 * @note    check power, check registered, check gprs active
 */
uint8_t OTSIM900Link::startGPRS()
{
  char data[min(16, MAX_SIM900_RESPONSE_CHARS)];
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush(F("GPRS"));
#endif // OTSIM900LINK_DEBUG
  print(AT_START);
  print(AT_START_GPRS);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));


  // response stuff
//  const char *dataCut;
  uint8_t dataCutLength = 0;
  getResponse(dataCutLength, data, sizeof(data), 0x0A);    // unreliable
#ifdef OTSIM900LINK_DEBUG
  OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG
  if (dataCutLength == 9) return 0;    // expected response 'OK'
  else return -1;
}

/**
 * @brief   Shut GPRS connection
 * @retval  0 if shut
 *          -1 if failed to shut
 */
uint8_t OTSIM900Link::shutGPRS()
{
  char data[MAX_SIM900_RESPONSE_CHARS]; // FIXME: was 96: that's a LOT of stack!
  print(AT_START);
  print(AT_SHUT_GPRS);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));

  // response stuff
  const char *dataCut;
  uint8_t dataCutLength = 0;
  dataCut= getResponse(dataCutLength, data, sizeof(data), 0x0A);
  if (*dataCut == 'S') return 0;    // expected response 'SHUT OK'
  else return -1;
}

/**
 * @brief   Get IP address
 * @todo    How should I return the string?
 * @param   pointer to array to store IP address in. must be at least 16 characters long
 * @retval  return length of IP address. Return 0 if no connection
 */
uint8_t OTSIM900Link::getIP()
{
  char data[MAX_SIM900_RESPONSE_CHARS];
  print(AT_START);
  print(AT_GET_IP);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));
  // response stuff
  const char *dataCut;
  uint8_t dataCutLength = 0;
  dataCut= getResponse(dataCutLength, data, sizeof(data), 0x0A);

  if(*dataCut == '+') return 0; // all error messages will start with a '+'
  else {
      return dataCutLength;
  }
}

/**
 * @brief    check if UDP open
 * @todo    implement function
 * @retval    true if open
 */
uint8_t OTSIM900Link::isOpenUDP()
{
    char data[MAX_SIM900_RESPONSE_CHARS];
    print(AT_START);
    print(AT_STATUS);
    print(AT_END);
    timedBlockingRead(data, sizeof(data));

#ifdef OTSIM900LINK_DEBUG
  OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG

    // response stuff
    const char *dataCut;
    uint8_t dataCutLength = 0;
    dataCut = getResponse(dataCutLength, data, sizeof(data), ' '); // first ' ' appears right before useful part of message
    if (*dataCut == 'C') return 1; // expected string is 'CONNECT OK'. no other possible string begins with R
    else if (*dataCut == 'P') return 2;
    else return false;
}

/**
 * @brief   Set verbose errors
 * @todo    What will be done with this?
 *             Change level to enum
 */
void OTSIM900Link::verbose(uint8_t level)
{
  char data[MAX_SIM900_RESPONSE_CHARS];
  print(AT_START);
  print(AT_VERBOSE_ERRORS);
  print(AT_SET);
  print((char)(level + '0')); // 0: no error codes, 1: error codes, 2: full error descriptions
  print(AT_END);
  timedBlockingRead(data, sizeof(data));
#ifdef OTSIM900LINK_DEBUG
  OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG
}

/**
 * @brief   Enter PIN code
 * @param   pin     pointer to array containing pin code
 * @param   length  length of pin
 * @todo    implement check
 */
uint8_t OTSIM900Link::setPIN()
{
  char data[MAX_SIM900_RESPONSE_CHARS];
  print(AT_START);
  print(AT_PIN);
  print(AT_SET);
  print(config->PIN);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));

#ifdef OTSIM900LINK_DEBUG
  OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG
  return 0;
}

/**
 * @brief   Check if PIN required
 * @todo    return logic
 */
bool OTSIM900Link::checkPIN()
{
  char data[min(40, MAX_SIM900_RESPONSE_CHARS)];
  print(AT_START);
  print(AT_PIN);
  print(AT_QUERY);
  print(AT_END);
  timedBlockingRead(data, sizeof(data));

  // response stuff
  const char *dataCut;
  uint8_t dataCutLength = 0;
  dataCut = getResponse(dataCutLength, data, sizeof(data), ' '); // first ' ' appears right before useful part of message
  if (*dataCut == 'R') return true; // expected string is 'READY'. no other possible string begins with R
  else return false;
}

/**
 * @brief    Returns a pointer to section of response containing important data
 *             and sets its length to a variable
 * @param    newLength    length of useful data
 * @param    data        pointer to array containing response from device
 * @param    dataLength    length of array
 * @param    startChar    ignores everything up to and including this character
 * @retval    pointer to start of useful data
 */
const char *OTSIM900Link::getResponse(uint8_t &newLength, const char *data, uint8_t dataLength, char _startChar)
{
    char startChar = _startChar;
    const char *newPtr = NULL;
    uint8_t  i = 0;    // 'AT' + command + 0x0D
    uint8_t i0 = 0; // start index
    newLength = 0;

    // Ignore echo of command
    while (*data !=  startChar) {
        data++;
        i++;
        if(i >= dataLength) return NULL;
    }
    data++;
    i++;

    // Set pointer to start of and index
    newPtr = data;
    i0 = i;

    // Find end of response
    while(*data != 0x0D) {    // find end of response
        data++;
        i++;
        if(i >= dataLength) return NULL;
    }

    newLength = i - i0;

#if 0 && defined(OTSIM900LINK_DEBUG)
    char *stringEnd = (char *)data;
     *stringEnd = '\0';
    OTV0P2BASE::serialPrintAndFlush(newPtr);
    OTV0P2BASE::serialPrintlnAndFlush();
#endif // OTSIM900LINK_DEBUG

    return newPtr;    // return length of new array
}

/**
 * @brief   Test if radio is available and set available and power flags
 *             returns to powered off state?
 * @retval  0 if module found and returns correct start value.
 *          -1 if failed
 * @todo    possible to just cycle power and read return val
 *          Lots of testing
 *          Get rid of blocking
 * @note    Blocks for too long
 *          Possible states at start up:
 *             1. no module - No response
 *             2. module not powered - No response
 *             3. module powered - correct response
 *             4. wrong module - unexpected response
 */
uint8_t OTSIM900Link::getInitState()
{
    // Test if available and set flags
    bAvailable = false;
    bPowered = false;
    char data[min(10, MAX_SIM900_RESPONSE_CHARS)];    // max expected response
    memset(data, 0 , sizeof(data));

    delay(1000); // To allow for garbage sent on startup

#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush("Module?");
#endif // OTSIM900LINK_DEBUG
    print(AT_START);
    print(AT_END);

    print(AT_START);
    print(AT_END);    // FIXME this is getting ugly
    if (timedBlockingRead(data, sizeof(data)) == 0) { // state 1 or 2

#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush("- Force On");
#endif // OTSIM900LINK_DEBUG

        powerToggle();
        memset(data, 0, sizeof(data));
        //flushUntil(0x0A);
        print(AT_START);
        print(AT_END);
        if (timedBlockingRead(data, sizeof(data)) == 0) { // state 1

#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush("-- Failed");
#endif // OTSIM900LINK_DEBUG

            bPowered = false;
            return -1;
        }
    } else if( data[0] == 'A' ) { // state 3 or 4
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintlnAndFlush("-- Success");
#endif // OTSIM900LINK_DEBUG
        bAvailable = true;
        bPowered = true;
        powerOff();
        return -1;    // state 3
    } else {
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintAndFlush("- Bad Response: ");
    OTV0P2BASE::serialPrintlnAndFlush(data);
#endif // OTSIM900LINK_DEBUG
        bAvailable = false;
        bPowered = false;
        return -1;    // state 4
    }
    return 0;
}

/**
 * @brief    get signal strength
 */
void OTSIM900Link::getSignalStrength()
{
#ifdef OTSIM900LINK_DEBUG
    OTV0P2BASE::serialPrintAndFlush(F("RSSI: "));
#endif // OTSIM900LINK_DEBUG
    char data[min(32, MAX_SIM900_RESPONSE_CHARS)];
    print(AT_START);
    print(AT_SIGNAL);
    print(AT_END);
    timedBlockingRead(data, sizeof(data));

    // response stuff
    uint8_t dataCutLength = 0;
    getResponse(dataCutLength, data, sizeof(data), ' '); // first ' ' appears right before useful part of message
}

/**
 * @brief    This will be called in interrupt while waiting for send prompt
 * @todo    Must do nothing if not in WAIT_FOR_PROMPT state
 *             in WAIT_FOR_PROMPT:
 *             - trigger if pin low
 *             - disable interrupts (where?)
 *             - set flag
 *             - enable interrupts
 *     @retval    returns true on successful exit
 */
bool OTSIM900Link::handleInterruptSimple()
{
//    if (state == WAIT_FOR_PROMPT) {
//        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
//            if(read() == '>') FLAG GOES HERE; // '>' is prompt
//        }
//    }
    return true;
}

//const char OTSIM900Link::AT_[] = "";
const char OTSIM900Link::AT_START[3] = "AT";
const char OTSIM900Link::AT_SIGNAL[5] = "+CSQ";
const char OTSIM900Link::AT_NETWORK[6] = "+COPS";
const char OTSIM900Link::AT_REGISTRATION[6] = "+CREG"; // GSM registration.
const char OTSIM900Link::AT_GPRS_REGISTRATION0[7] = "+CGATT"; // GPRS registration.
const char OTSIM900Link::AT_GPRS_REGISTRATION[7] = "+CGREG"; // GPRS registration.
const char OTSIM900Link::AT_SET_APN[6] = "+CSTT";
const char OTSIM900Link::AT_START_GPRS[7] = "+CIICR";
const char OTSIM900Link::AT_GET_IP[7] = "+CIFSR";
const char OTSIM900Link::AT_PIN[6] = "+CPIN";

const char OTSIM900Link::AT_STATUS[11] = "+CIPSTATUS";
const char OTSIM900Link::AT_START_UDP[10] = "+CIPSTART";
const char OTSIM900Link::AT_SEND_UDP[9] = "+CIPSEND";
const char OTSIM900Link::AT_CLOSE_UDP[10] = "+CIPCLOSE";
const char OTSIM900Link::AT_SHUT_GPRS[9] = "+CIPSHUT";

const char OTSIM900Link::AT_VERBOSE_ERRORS[6] = "+CMEE";

// tcpdump -Avv udp and dst port 9999


} // OTSIM900Link
