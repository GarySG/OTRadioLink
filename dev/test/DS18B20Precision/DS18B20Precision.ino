#include <Wire.h>
#include <OTRadioLink.h>

#define DS18B20_MODEL_ID 0x28

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

OTV0P2BASE::MinimalOneWire<> minOW;

static const uint8_t setPrecision(const uint8_t address[8], const uint8_t precision)
{
  const uint8_t write = ((precision - 9) << 5) | 0x1f;
  
  // Write scratchpad/config.
#if 1
  minOW.reset();
#if 0
    minOW.select(address);
#else
    minOW.skip();
#endif
  minOW.write(CMD_WRITE_SCRATCH);
  minOW.write(0); // Th: not used.
  minOW.write(0); // Tl: not used.
  minOW.write(write); // Config register; lsbs all 1.
#endif
  minOW.reset();
  minOW.select(address);
  minOW.write(CMD_READ_SCRATCH);

  uint8_t d = 0;
  for (uint8_t i = 0; i <= LOC_CONFIGURATION; i++) {
    d = minOW.read();
  }
  return d;
}

static int16_t readTemp(const uint8_t address[8])
{
  minOW.reset();
  minOW.select(address);
  minOW.write(CMD_READ_SCRATCH);

  // Read first two bytes of 9 available.  (No CRC config or check.)
  const uint8_t d0 = minOW.read();
  const uint8_t d1 = minOW.read();

  // Extract raw temperature, masking any undefined lsbit.
  // TODO: mask out undefined LSBs if precision not maximum.
  return (d1 << 8) | (d0);
}

uint8_t address[8];

void setup()
{
  Serial.begin(4800);

  // Ensure no bad search state.
  minOW.reset_search();

  while (minOW.search(address))
  {
    if(DS18B20_MODEL_ID == address[0]) {
      break;
    }
  }
  minOW.reset_search();

  //OTV0P2BASE::serialPrintAndFlush("p,9,ms,p,10,ms,p,11,ms,p,12,ms");
  //OTV0P2BASE::serialPrintlnAndFlush();
}
/*
Something odd happening where original readTemp seemed to return before temp updated?
By monitoring reported temp changes at each precision 
precision  9: < 114 ms *4 131-279/377 ms .: ~49 ms/read ((279-131) / 3)
precision 10: < 163 ms *4 213-360/459 ms .: ~49 ms/read ((360-213) / 3)
precision 11: < 311 ms *4 376-541/606 ms .: ~55 ms/read ((541-376) / 3)
precision 12: < 622 ms *4 671-852/934 ms .: ~60 ms/read ((852-671) / 3)
IMHO this emphasises further the need to seperate capture/conversion from collection
What was wrong was 
a) conversion wait was part of read temp, 
b) most importantly conversion wait is supposed to be 
part of the DS18B20 general and necessary conversion dialogue rather than a 
distinct, unnecessary and ill formed DS18B20 address dialogue 
*/
void loop()
{
  
  int16_t t[10];
  uint8_t k = 0U;
  for (; 10 > k; ++k)
  {
    t[k] = 0;
  }
  for (uint8_t precision = OTV0P2BASE::TemperatureC16_DS18B20::MIN_PRECISION;
       precision <= OTV0P2BASE::TemperatureC16_DS18B20::MAX_PRECISION;
       precision++)
  {
    // Set the precision
    const uint8_t d = setPrecision(address, precision);

    OTV0P2BASE::serialPrintAndFlush(d, HEX);
    OTV0P2BASE::serialPrintlnAndFlush();

    // Try four separate conversions
    for (uint8_t j = 0; 4 > j; ++j)
    {
    unsigned long time = millis();
    
    // Convert the temp
    minOW.reset();
// Use convert all selection (otherwise specific address)
#if 0
    minOW.select(address);
#else
    minOW.skip();
#endif
    minOW.write(CMD_START_CONVO); // Start conversion without parasite power.

// Diagnose number of waiting for conversion to end iterations
#if 1
    uint8_t bit = 0U;
#endif
    while (minOW.read_bit() == 0) {
#if 1
      ++bit;
#endif
// Use Arduino delay and millis for consistent timing behaviour
#if 0
      OTV0P2BASE::nap(WDTO_15MS);
#else
      delay(15);
#endif
    }
#if 1
    if (0U == bit)
    {
      //OTV0P2BASE::serialPrintAndFlush("0");
    }
    else
    {
      OTV0P2BASE::serialPrintAndFlush(bit);
      OTV0P2BASE::serialPrintAndFlush(",");
    }
#endif

    uint8_t k = 0U;
    while (minOW.search(address))
    {
    // Is this a DS18B20?
    if (DS18B20_MODEL_ID != address[0]) {
      continue;
    }

    // Collect the temp many times (with delays) reporting any temp changes to demonstrate correct behaviour
    for (uint8_t i = 0; 100 > i;++i)
    {
    int16_t temp = readTemp(address);
    unsigned long dly = millis() - time;

    //if (OTV0P2BASE::TemperatureC16_DS18B20::MIN_PRECISION != precision) {
    //  OTV0P2BASE::serialPrintAndFlush(",");
    //}
    if (t[k] != temp)
    {
      t[k] = temp;
      OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
      OTV0P2BASE::serialPrintAndFlush('.');
      OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
      OTV0P2BASE::serialPrintAndFlush(",");
      OTV0P2BASE::serialPrintAndFlush(dly, DEC);
      OTV0P2BASE::serialPrintAndFlush(",");
    }
    
    // HACK: just once.
    break;
    
    delay(10);
    }
    OTV0P2BASE::serialPrintlnAndFlush();
    ++k;
    }
    }
  }
  delay(1000);
}
