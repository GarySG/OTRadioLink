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

Author(s) / Copyright (s): Damon Hart-Davis 2014--2016
                           John Harvey 2014 (DS18B20 code)
                           Deniz Erbilgin 2015--2016
                           Jeremy Poulter 2016
                           Gary Gladman 2016
*/

/*
 DS18B20 OneWire(TM) temperature detector.
 */

#include "OTV0P2BASE_SensorDS18B20.h"

#if defined(TemperatureC16_DS18B20_DEFINED)

 // Model IDs
#define DS18S20_MODEL_ID 0x10
#define DS18B20_MODEL_ID 0x28
#define DS1822_MODEL_ID  0x22

 // OneWire commands
#define CMD_START_CONVO       0x44  // Tells device to take a temperature reading and put it on the scratchpad
#define CMD_COPY_SCRATCH      0x48  // Copy EEPROM
#define CMD_READ_SCRATCH      0xBE  // Read EEPROM
#define CMD_WRITE_SCRATCH     0x4E  // Write to EEPROM
#define CMD_RECALL_SCRATCH    0xB8  // Reload from last known
#define CMD_READ_POWER_SUPPLY 0xB4  // Determine if device needs parasite power
#define CMD_ALARM_SEARCH      0xEC  // Query bus for devices with an alarm condition

 // Scratchpad locations
#define LOC_TEMP_LSB          0
#define LOC_TEMP_MSB          1
#define LOC_HIGH_ALARM_TEMP   2
#define LOC_LOW_ALARM_TEMP    3
#define LOC_CONFIGURATION     4
#define LOC_INTERNAL_BYTE     5
#define LOC_COUNT_REMAIN      6
#define LOC_COUNT_PER_C       7
#define LOC_SCRATCHPAD_CRC    8

 // Error Codes
#define DEVICE_DISCONNECTED   -127

// Override OTRV delays with use of Arduino delays so test harness timings tally.
// Normally undefined.
// #define ARDUINO_TIMING

namespace OTV0P2BASE
{


// Initialise the device (if any) before first use.
// Returns true iff successful.
// Uses specified order DS18B20 found on bus.
// May need to be reinitialised if precision changed.
bool TemperatureC16_DS18B20::init()
  {
#if 0 && defined(DEBUG)
  V0P2BASE_DEBUG_SERIAL_PRINT_FLASHSTRING("DS18B20 init ");
  V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif

  bool found = false;
  uint8_t count = 0;
  uint8_t address[8];

  // Ensure no bad search state.
  minOW.reset_search();

  while (minOW.search(address))
    {
#if 0 && defined(DEBUG)
    // Found a device.
    V0P2BASE_DEBUG_SERIAL_PRINT_FLASHSTRING("addr:");
    for(int i = 0; i < 8; ++i)
      {
      V0P2BASE_DEBUG_SERIAL_PRINT(' ');
      V0P2BASE_DEBUG_SERIAL_PRINTFMT(address[i], HEX);
      }
    V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif

    if(DS18B20_MODEL_ID != address[0])
      {
#if 0 && defined(DEBUG)
      V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING("Not a DS18B20, skipping...");
#endif
      continue;
      }

    // Found one and configured it!
    found = true;
    count++;

#if 0 && defined(DEBUG)
    V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING("Setting precision...");
#endif

    minOW.reset();
    // Write scratchpad/config
    minOW.select(address);
    minOW.write(CMD_WRITE_SCRATCH);
    minOW.write(0); // Th: not used.
    minOW.write(0); // Tl: not used.
    minOW.write(((precision - 9) << 5) | 0x1f); // Config register; lsbs all 1.
    }

#if 0 && defined(DEBUG)
  V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING("No more devices...");
#endif
  minOW.reset_search(); // Be kind to any other OW search user.

  // Search has been run (whether DS18B20 was found or not).
  initialised = true;

  sensorCount = count;
  return(found);
  }

// Force a capture of temperature from potentially multiple DS18B20 sensors.
// The return indicates whether 0 or more sensors are capturing.
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
uint16_t TemperatureC16_DS18B20::capture(void)
  {
  if(!initialised) { init(); }
  if(0U == sensorCount) { return(0); }

  // Start a temperature reading.
  minOW.reset();
  minOW.skip();
  minOW.write(CMD_START_CONVO); // Start conversion without parasite power.

  return (1);
  }

// Extract temperature from multiple DS18B20 sensors providing a capture has been initiated.
// The value sensed, in nominal units of 1/16 C, is written to the array of uint16_t (with count elements)
// pointed to by values. The values are written in the order they are found on the One-Wire bus.
// index specifies the sensor to start reading at 0 being the first. This can be used to read more sensors
// than elements in the values array
// The return is the number of values read
// At sub-maximum precision lsbits will be zero or undefined.
// Expensive/slow - NOTE: once capture is initiated other activities can be interleaved prior to extraction
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
uint16_t TemperatureC16_DS18B20::extractMultiple(int16_t *values, int count, int index) const
  {
  int sensor = 0;

  // Poll for conversion complete (bus released)...
  // FIXME: don't allow indefinite blocking.
  while (minOW.read_bit() == 0)
    {
#if defined(ARDUINO_TIMING)
    delay(15);                  // play nicely with millis
#else
    OTV0P2BASE::nap(WDTO_15MS); // proper
#endif
    }

  // Ensure no bad search state.
  minOW.reset_search();

  uint8_t address[8];
  while (minOW.search(address))
    {
    // Is this a DS18B20?
    if (DS18B20_MODEL_ID != address[0])
      {
#if 0 && defined(DEBUG)
      V0P2BASE_DEBUG_SERIAL_PRINTLN_FLASHSTRING("Not a DS18B20, skipping...");
#endif
      continue;
      }

    // Have we reached the first sensor we are interested in
    if (index > 0)
      {
      index--;
      continue;
      }

    // Fetch temperature (scratchpad read).
    minOW.reset();
    minOW.select(address);
    minOW.write(CMD_READ_SCRATCH);

    // Read first two bytes of 9 available.  (No CRC config or check.)
    const uint8_t d0 = minOW.read();
    const uint8_t d1 = minOW.read();
    // Terminate read and let DS18B20 go back to sleep.
    minOW.reset();

    // Extract raw temperature, masking any undefined lsbit.
    // TODO: mask out undefined LSBs if precision not maximum.
    const int16_t rawC16 = (d1 << 8) | (d0);

    // Return corrected temperatures.
    values[sensor] = rawC16 + correction[sensor];
    ++sensor;

    // Do we have any space left
    if (sensor >= count) { break; }
    }

  return(sensor);
  }

// Force a capture and extraction of temperature from multiple DS18B20 sensors.
// The value sensed, in nominal units of 1/16 C, is written to the array of uint16_t (with count elements)
// pointed to by values. The values are written in the order they are found on the One-Wire bus.
// index specifies the sensor to start reading at 0 being the first. This can be used to read more sensors
// than elements in the values array
// The return is the number of values read
// At sub-maximum precision lsbits will be zero or undefined.
// Expensive/slow NOTE: capture and extraction can be seperated
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
uint16_t TemperatureC16_DS18B20::readMultiple(int16_t *values, int count, int index)
  {
  return capture() ? extractMultiple(values, count, index) : 0;
  }

// Force capture and extraction of temperature from the single DS18B20 sensor.
// Return the value sensed in nominal units of 1/16 C.
// At sub-maximum precision lsbits will be zero or undefined.
// Expensive/slow NOTE: capture and extraction can be seperated
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
int16_t TemperatureC16_DS18B20::read()
  {
  if(1 == readMultiple(&value, 1))
    {
    return(value);
    }

  value = DEFAULT_INVALID_TEMP;
  return(DEFAULT_INVALID_TEMP);
  }

uint8_t TemperatureC16_DS18B20::getSensorCount()
  {
  if (!initialised) { init(); }
  return sensorCount;
  }

// Calculate the per sensor correction for a number of sensors.
// Assumes n co-located temperature sensors at ambient prior to relocating and setting to work.
// Expected to be used once during system setup.
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
void TemperatureC16_DS18B20::correct(int8_t * const corr)
  {
  // Average sensor readings to derive and thus correct for individual sensor error.

  const uint8_t n = getSensorCount();

  // Obtain uncorrected readings.
  memset(correction, 0, sizeof(correction));

  // Obtain a number of samples for each sensor.
  int16_t d;
  for(uint8_t k = 0U; 4U > k; ++k)
    {
    int16_t values[n];
    readMultiple(values, n);

    // Capture the very first value as a basis for relative calculations.
    if (0U == k)
      {
      d = values[0];
      }

    // Sum values (and increase significance).
    // Relative rather than absolute values for performance.
    int8_t v[n];
    int8_t sum = 0;
    for(uint8_t i = 0U; n > i; ++i)
      {
      v[i] = (uint8_t)(values[i] - d) * 4U;
      sum += v[i];
#if 0 && defined(DEBUG)
      V0P2BASE_DEBUG_SERIAL_PRINT(values[i]);
      V0P2BASE_DEBUG_SERIAL_PRINT(',');
#endif
      }
#if 0 && defined(DEBUG)
    V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif
    // calculate rounded average
    const int8_t avg = (((sum + (n / 2)) / n) + 2)
#if defined(DS18B20_STAT_CORRECTION)
    + 2;  // including simple statistical correction (2) for DS18B20's
#else
    ;
#endif
#if 0 && defined(DEBUG)
    V0P2BASE_DEBUG_SERIAL_PRINT(avg);
    V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif

    // Determine per sensor correction.
    for(uint8_t i = 0U; n > i; ++i)
      {
      const int8_t c = avg - v[i];
      corr[i] = (0U == k) ? c : (corr[i] + c) / 2;    // exponential moving average (1:1)
#if 0 && defined(DEBUG)
      V0P2BASE_DEBUG_SERIAL_PRINT(corr[i]);
      V0P2BASE_DEBUG_SERIAL_PRINT(',');
#endif
      }
#if 0 && defined(DEBUG)
    V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif
    }

  // Finally round and store (and reduce significance).
  for(uint8_t i = 0U; n > i; ++i)
    {
    corr[i] = (corr[i] + ((0 <= corr[i]) ? (+4 / 2) : (-4 / 2))) / 4;    // round away from zero
    correction[i] = corr[i];
#if 0 && defined(DEBUG)
    V0P2BASE_DEBUG_SERIAL_PRINT(corr[i]);
    V0P2BASE_DEBUG_SERIAL_PRINT(',');
#endif
    }
#if 0 && defined(DEBUG)
  V0P2BASE_DEBUG_SERIAL_PRINTLN();
#endif
  }

// Set the per sensor correction for a number of sensors.
// Not thread-safe nor usable within ISRs (Interrupt Service Routines).
void TemperatureC16_DS18B20::setCorrect(const int8_t * const corr)
  {
  memcpy(correction, corr, sizeof(correction));
  }

}

#endif // defined(TemperatureC16_DS18B20_DEFINED)
