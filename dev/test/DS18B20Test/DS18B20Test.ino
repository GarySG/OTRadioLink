#include <Wire.h>
#include <OTV0p2Base.h>

OTV0P2BASE::HumiditySensorSHT21 RelHumidity;
OTV0P2BASE::RoomTemperatureC16_SHT21 TemperatureC16; // SHT21 impl.

OTV0P2BASE::MinimalOneWire<> MinOW_DEFAULT;

OTV0P2BASE::TemperatureC16_DS18B20 extDS18B20 = OTV0P2BASE::TemperatureC16_DS18B20(MinOW_DEFAULT, OTV0P2BASE::TemperatureC16_DS18B20::MAX_PRECISION);
int numberDS18B20 = 0;
int8_t corr[OTV0P2BASE::TemperatureC16_DS18B20::MAX_SENSORS];

#define ARRAY_LENGTH(x)  (sizeof((x))/sizeof((x)[0]))

void setup()
{
    Serial.begin(4800);
    OTV0P2BASE::serialPrintlnAndFlush("Temp/Humidity test (MAX_PRECISION)");

    numberDS18B20 = extDS18B20.getSensorCount();
    OTV0P2BASE::serialPrintAndFlush("number DS18B20: ");
    OTV0P2BASE::serialPrintAndFlush(numberDS18B20, DEC);
    OTV0P2BASE::serialPrintlnAndFlush();

    unsigned long time = millis();
    uint8_t address[8];
    MinOW_DEFAULT.reset_search();
    while (MinOW_DEFAULT.search(address))
    {
      unsigned long dly = millis() - time;
        OTV0P2BASE::serialPrintAndFlush("addr: ");
        for (int i = 0; i < 8; ++i)
        {
            OTV0P2BASE::serialPrintAndFlush(' ');
            OTV0P2BASE::serialPrintAndFlush(address[i], HEX);
        }
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();

        time = millis();
    }
    MinOW_DEFAULT.reset_search();

}

void loop()
{
    OTV0P2BASE::serialPrintAndFlush("NB: Arduino and OpenTRV do not play nicely regarding time so timings will appear short");
    OTV0P2BASE::serialPrintAndFlush(" ... unless ARDUINO_TIMING is defined temporarily in OTV0PBASE_SensorDS18B20.cpp");
    OTV0P2BASE::serialPrintlnAndFlush();
    
    unsigned long time = millis();
    int16_t temp = TemperatureC16.read();
    unsigned long dly = millis() - time;
    OTV0P2BASE::serialPrintAndFlush("SHT21 temp: ");
    OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
    OTV0P2BASE::serialPrintAndFlush('.');
    OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
    OTV0P2BASE::serialPrintAndFlush('C');
    OTV0P2BASE::serialPrintAndFlush(" in ");
    OTV0P2BASE::serialPrintAndFlush(dly, DEC);
    OTV0P2BASE::serialPrintAndFlush("ms");
    OTV0P2BASE::serialPrintlnAndFlush();

    if (RelHumidity.isAvailable())
    {
        time = millis();
        int16_t humid = TemperatureC16.read();
        dly = millis() - time;
        OTV0P2BASE::serialPrintAndFlush("humidity: ");
        OTV0P2BASE::serialPrintAndFlush(humid, DEC);
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate read functionality.
    OTV0P2BASE::serialPrintAndFlush("read() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    time = millis();
    temp = extDS18B20.read();
    dly = millis() - time;
    OTV0P2BASE::serialPrintAndFlush("DS18B20 (first): ");
    OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
    OTV0P2BASE::serialPrintAndFlush('.');
    OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
    OTV0P2BASE::serialPrintAndFlush('C');
    OTV0P2BASE::serialPrintAndFlush(" in ");
    OTV0P2BASE::serialPrintAndFlush(dly, DEC);
    OTV0P2BASE::serialPrintAndFlush("ms");
    OTV0P2BASE::serialPrintlnAndFlush();

    // Demonstrate readMultiple functionality.
    OTV0P2BASE::serialPrintAndFlush("readMultiple() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    int16_t allTemp[OTV0P2BASE::TemperatureC16_DS18B20::MAX_SENSORS];
    time = millis();
    numberDS18B20 = extDS18B20.readMultiple(allTemp, ARRAY_LENGTH(allTemp));
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] temp: ");
        temp = allTemp[i];
        OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
        OTV0P2BASE::serialPrintAndFlush('.');
        OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
        OTV0P2BASE::serialPrintAndFlush('C');
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate distinct capture/extractMultiple functionality with long injected delay.
    OTV0P2BASE::serialPrintAndFlush("capture()+over sufficient 1000ms delay+extractMultiple() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    time = millis();
    uint16_t ok;
    ok = extDS18B20.capture();
    dly = millis() - time;

    OTV0P2BASE::serialPrintAndFlush("Capture ");
    OTV0P2BASE::serialPrintAndFlush(ok);
    OTV0P2BASE::serialPrintAndFlush(" in ");
    OTV0P2BASE::serialPrintAndFlush(dly, DEC);
    OTV0P2BASE::serialPrintAndFlush("ms");
    OTV0P2BASE::serialPrintlnAndFlush();

    delay(1000);
     
    time = millis();
    numberDS18B20 = extDS18B20.extractMultiple(allTemp, ARRAY_LENGTH(allTemp));
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] temp: ");
        temp = allTemp[i];
        OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
        OTV0P2BASE::serialPrintAndFlush('.');
        OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
        OTV0P2BASE::serialPrintAndFlush('C');
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate distinct capture/extractMultiple functionality with not long enough injected delay.
    OTV0P2BASE::serialPrintAndFlush("capture()+insufficient 500ms delay+extractMultiple() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    time = millis();
    ok = extDS18B20.capture();
    dly = millis() - time;

    OTV0P2BASE::serialPrintAndFlush("Capture ");
    OTV0P2BASE::serialPrintAndFlush(ok);
    OTV0P2BASE::serialPrintAndFlush(" in ");
    OTV0P2BASE::serialPrintAndFlush(dly, DEC);
    OTV0P2BASE::serialPrintAndFlush("ms");
    OTV0P2BASE::serialPrintlnAndFlush();

    delay(500);
     
    time = millis();
    numberDS18B20 = extDS18B20.extractMultiple(allTemp, ARRAY_LENGTH(allTemp));
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] temp: ");
        temp = allTemp[i];
        OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
        OTV0P2BASE::serialPrintAndFlush('.');
        OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
        OTV0P2BASE::serialPrintAndFlush('C');
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate calculation of correction
    OTV0P2BASE::serialPrintAndFlush("correct() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    time = millis();
    extDS18B20.correct(corr);
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] corr: ");
        temp = corr[i];
        OTV0P2BASE::serialPrintAndFlush(temp, DEC);
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate corrected readMultiple functionality.
    OTV0P2BASE::serialPrintAndFlush("readMultiple() temp corrected ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    time = millis();
    numberDS18B20 = extDS18B20.readMultiple(allTemp, ARRAY_LENGTH(allTemp));
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] temp: ");
        temp = allTemp[i];
        OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
        OTV0P2BASE::serialPrintAndFlush('.');
        OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
        OTV0P2BASE::serialPrintAndFlush('C');
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }

    // Demonstrate setCorrected readMultiple functionality.
    // Intention is to recover and load recorded correction post system setup.
    OTV0P2BASE::serialPrintAndFlush("setCorrection() + readMultiple() ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    extDS18B20.setCorrect(corr);
    
    // ... thereafter ...
    time = millis();
    numberDS18B20 = extDS18B20.readMultiple(allTemp, ARRAY_LENGTH(allTemp));
    dly = millis() - time;

    for (int i = 0; i < numberDS18B20; ++i)
    {
        OTV0P2BASE::serialPrintAndFlush("DS18B20[");
        OTV0P2BASE::serialPrintAndFlush(i);
        OTV0P2BASE::serialPrintAndFlush("] temp: ");
        temp = allTemp[i];
        OTV0P2BASE::serialPrintAndFlush(temp >> 4, DEC);
        OTV0P2BASE::serialPrintAndFlush('.');
        OTV0P2BASE::serialPrintAndFlush((unsigned long)(((temp & 0xf) / 16.0) * 1000), DEC);
        OTV0P2BASE::serialPrintAndFlush('C');
        OTV0P2BASE::serialPrintAndFlush(" in ");
        OTV0P2BASE::serialPrintAndFlush(dly, DEC);
        OTV0P2BASE::serialPrintAndFlush("ms");
        OTV0P2BASE::serialPrintlnAndFlush();
    }
    
    // Special: reset extDS18B20 for next test cycle.
    OTV0P2BASE::serialPrintAndFlush("correction unset for next iteration ...");
    OTV0P2BASE::serialPrintlnAndFlush();
    memset(corr, 0, sizeof(corr));
    extDS18B20.setCorrect(corr);

}
