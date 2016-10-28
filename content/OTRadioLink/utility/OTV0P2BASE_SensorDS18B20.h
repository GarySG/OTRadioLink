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
                           Jeremy Poulter 2016
                           Gary Gladman 2016
*/

/*
 DS18B20 OneWire(TM) temperature detector.
 */

#ifndef OTV0P2BASE_SENSORDS18B20_H
#define OTV0P2BASE_SENSORDS18B20_H

//#include "OTV0P2BASE_Util.h"
#include "OTV0P2BASE_MinOW.h"
#include "OTV0P2BASE_Sensor.h"
#include "utility/OTV0P2BASE_SensorTemperatureC16Base.h"


namespace OTV0P2BASE
{


#ifdef ARDUINO_ARCH_AVR
#if defined(MinimalOneWireBase_DEFINED) // Required definition.
// External/off-board DS18B20 temperature sensor in nominal 1/16 C.
// Requires OneWire support.
// Will in future be templated on:
//   * the MinimalOneWire instance to use
//   * precision (9, 10, 11 or 12 bits, 12 for the full C/16 resolution),
//     noting that lower precision is faster,
//     and for example 1C will be 0x1X
//     with more bits of the final nibble defined for with higher precision
//   * enumeration order of this device on the OW bus,
//     with 0 (the default) being the first found by the usual deterministic scan
//   * whether the CRC should de checked for incoming data
//     to improve reliability on long connections at a code and CPU cost
// Multiple DS18B20s can nominally be supported on one or multiple OW buses.
// Not all template parameter combinations may be supported.
// Provides temperature as a signed int value with 0C == 0 at all precisions.
#define TemperatureC16_DS18B20_DEFINED
class TemperatureC16_DS18B20 : public TemperatureC16Base
  {
  public:
    // Maximum number of corrected sensors.
    static const uint8_t MAX_SENSORS = 10U;

  private:
    // Reference to minimal OneWire support instance for appropriate GPIO.
    OTV0P2BASE::MinimalOneWireBase &minOW;

    // True once initialised.
    bool initialised;

    // Precision in range [9,12].
    const uint8_t precision;

    // The number of sensors found on the bus
    uint8_t numberSensors;

    // Per sensor error correction.
    int8_t correction[MAX_SENSORS];

    // Initialise the device (if any) before first use.
    // Returns true iff successful.
    // Uses specified order DS18B20 found on bus.
    // May need to be reinitialised if precision changed.
    bool init();

  public:
    // Minimum supported precision, in bits, corresponding to 1/2 C resolution.
    static const uint8_t MIN_PRECISION = 9U;
    // Maximum supported precision, in bits, corresponding to 1/16 C resolution.
    static const uint8_t MAX_PRECISION = 12U;
    // Default precision; defaults to minimum for speed.
    static const uint8_t DEFAULT_PRECISION = MIN_PRECISION;

    // Returns number of useful binary digits after the binary point.
    // 8 less than total precision for DS18B20.
    virtual int8_t getBitsAfterPoint() const { return(precision - 8U); }

    // Returns true if this sensor is definitely unavailable or behaving incorrectly.
    // This is after an attempt to initialise has not found a DS18B20 on the bus.
    virtual bool isUnavailable() const { return(initialised && 0U == numberSensors); }

    // Create instance with given OneWire connection, bus ordinal and precision.
    // No two instances should attempt to target the same DS18B20,
    // though different DS18B20s on the same bus or different buses is allowed.
    // Precision defaults to minimum (9 bits, 0.5C resolution) for speed.
    TemperatureC16_DS18B20(OTV0P2BASE::MinimalOneWireBase &ow, uint8_t _precision = DEFAULT_PRECISION)
      : minOW(ow), initialised(false), precision(constrain(_precision, MIN_PRECISION, MAX_PRECISION))
#if defined(DS18B20_STAT_CORRECTION)
      { memset(correction, 2, sizeof(correction)); }
#else
      { memset(correction, 0, sizeof(correction)); }
#endif

    // Get current precision in bits [9,12]; 9 gives 1/2C resolution, 12 gives 1/16C resolution.
    uint8_t getPrecisionBits() const { return(precision); }

    // return the number of DS18B20 sensors on the bus
    uint8_t getNumberSensors();

    // Force capture and extraction of temperature from the single DS18B20 sensor.
    // Return the value sensed in nominal units of 1/16 C.
    // At sub-maximum precision lsbits will be zero or undefined.
    // Expensive/slow NOTE: capture and extraction can be separated
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    virtual int16_t read();

    // Force a capture of temperature from potentially multiple DS18B20 sensors.
    // The return indicates whether 0 or more sensors are capturing.
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    uint16_t capture(void);

    // Extract temperature from multiple DS18B20 sensors providing a capture has been initiated.
    // The value sensed, in nominal units of 1/16 C, is written to the array of uint16_t (with count elements)
    // pointed to by values. The values are written in the order they are found on the One-Wire bus.
    // index specifies the sensor to start reading at 0 being the first. This can be used to read more sensors
    // than elements in the values array
    // The return is the number of values read
    // At sub-maximum precision lsbits will be zero or undefined.
    // Expensive/slow - NOTE: once capture capture is initiated other activities can be interleaved prior to extraction
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    uint16_t extractMultiple(int16_t *values, int count, int index = 0) const;

    // Force capture and extraction of temperature from multiple DS18B20 sensors.
    // The value sensed, in nominal units of 1/16 C, is written to the array of uint16_t (with count elements)
    // pointed to by values. The values are written in the order they are found on the One-Wire bus.
    // index specifies the sensor to start reading at 0 being the first. This can be used to read more sensors
    // than elements in the values array
    // The return is the number of values read
    // At sub-maximum precision lsbits will be zero or undefined.
    // Expensive/slow NOTE: capture and extraction can be separated
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    uint16_t readMultiple(int16_t *values, int count, int index = 0);

    // Calculate the per sensor correction for a number of sensors.
    // Assumes n co-located temperature sensors at ambient prior to relocating and setting to work.
    // Expected to be used once during system setup.
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    void correct(int8_t * const corr);

    // Set the per sensor correction for a number of sensors.
    // Not thread-safe nor usable within ISRs (Interrupt Service Routines).
    void setCorrect(const int8_t * const corr);

  };
#endif // defined(MinimalOneWireBase_DEFINED) // Required definition.
#endif // ARDUINO_ARCH_AVR
}
#endif
