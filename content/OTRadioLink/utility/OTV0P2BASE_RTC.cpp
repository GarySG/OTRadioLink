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

Author(s) / Copyright (s): Damon Hart-Davis 2013--2015
*/

/*
 Real-time clock support AND RTC-connected watchdog/reset.
 */


#include <util/atomic.h>

#include <Arduino.h>

#include "OTV0P2BASE_EEPROM.h"
#include "OTV0P2BASE_Sleep.h"

#include "OTV0P2BASE_RTC.h"


namespace OTV0P2BASE
{


// Seconds for local time (and assumed UTC) in range [0,59].
// Volatile to allow for async update.
// Maintained locally or shadowed from external RTC.
// Read and write accesses assumed effectively atomic.
// NOT FOR DIRECT ACCESS OUTSIDE RTC ROUTINES.
volatile uint_fast8_t _secondsLT;

// Minutes since midnight for local time in range [0,1439].
// Must be accessed with interrupts disabled and as if volatile.
// Maintained locally or shadowed from external RTC.
// NOT FOR DIRECT ACCESS OUTSIDE RTC ROUTINES.
volatile uint_least16_t _minutesSinceMidnightLT;

// Whole days since the start of 2000/01/01 (ie the midnight between 1999 and 2000), local time.
// Must be accessed with interrupts disabled and as if volatile.
// This will roll in about 2179.
// NOT FOR DIRECT ACCESS OUTSIDE RTC ROUTINES.
volatile uint_least16_t _daysSince1999LT;


// The encoding for the persisted HH:MM value is as follows.
// The top 5 bits are the hour in the range [0,23].
// The bottom 3 bits indicate the quarter hour as follows:
// 111 => :00, 110 => :15, 100 => :30, 000 => :45.
// Invalid values (in particular, 0xff, for an erased byte) are ignored.
// On the hour the full byte is erased and written, including all the lsbits at 1.
// At each quarter hour one of the lsbits is written to zero (no erase is needed).
// Thus an hour causes 1 erase and 4 writes (3 of which only affect one bit each).
// The AVR EEPROM is rated for 100k cycles per byte (or page, not clear from docs),
// where a cycle would normally be 1 erase and 1 write.
// At worst, providing that no redundant writes are done,
// this causes 35k operations per year for ~3 years of continuous operation.
// If changing the bits is the stressful part that wears the EEPROM,
// and given that each bit only sees one erase and (at most) one subsequent write to 0 each hour,
// it may be reasonable to hope for upwards of 12 years of operation,
// in which time the Flash program and other EEPROM contents may have evaporated anyway.
// It is best to keep this byte in an EEPROM page without any other critical data
// and/or that is subject to significant erase/write cycles of its own,
// and where bytes may not be truely independent for wear purposes.

// Persist software RTC information to non-volatile (EEPROM) store.
// This does not attempt to store full precision of time down to seconds,
// but enough to help avoid the clock slipping too much during (say) a battery change.
// There is no point calling this more than (say) once per minute,
// though it will simply return relatively quickly from redundant calls.
// The RTC data is stored so as not to wear out AVR EEPROM for at least several years.
// IMPLEMENTATION OF THIS AND THE eeprom_smart_xxx_byte() ROUTINES IS CRITICAL TO PERFORMANCE AND LONGEVITY.
void persistRTC()
  {
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    {
    uint8_t quarterHours = (_minutesSinceMidnightLT / 15);
    uint8_t targetByte = (quarterHours << 1) & ~7U; // Bit pattern now hhhhh000 where hhhhh is whole hours [0,23].
    switch(quarterHours & 3)
      {
      case 0: targetByte |= 7; break;
      case 1: targetByte |= 3; break;
      case 2: targetByte |= 1; break;
      }

    // Update if target HH:MM not already correct.
    const uint8_t persistedValue = eeprom_read_byte((uint8_t*)V0P2BASE_EE_START_RTC_HHMM_PERSIST);
    if(persistedValue != targetByte)
      {
      // Where it is not possible to get the target value just by setting bits to 0,
      // eg for a new hour (ie completely different hour to that in EEPROM and on roll to new hour),
      // then do a full erase/write...
      //if((0 == quarterHours) || ((persistedValue & 0xf8) != (targetByte & 0xf8)))
      if(targetByte != (persistedValue & targetByte))
        { eeprom_write_byte((uint8_t*)V0P2BASE_EE_START_RTC_HHMM_PERSIST, targetByte); }
      // Else do a write without erase, typically clearing the quarter bits one at a time...
      else
        { eeprom_smart_clear_bits((uint8_t*)V0P2BASE_EE_START_RTC_HHMM_PERSIST, targetByte); }

      // Also persist the current days if not up to date.
      const uint16_t days = eeprom_read_word((uint16_t*)V0P2BASE_EE_START_RTC_DAY_PERSIST);
      if(days != _daysSince1999LT) { eeprom_write_word((uint16_t*)V0P2BASE_EE_START_RTC_DAY_PERSIST, _daysSince1999LT); }
      }
    }
  }

// Restore software RTC information from non-volatile (EEPROM) store, if possible.
// Returns true if the persisted data seemed valid and was restored, in full or part.
// To void on average using 15/2 minutes at each reset/restart,
// this starts the internal time a little over half way into the restored 15-minute slot.
// This restores the minutes and above but leaves seconds unset.
bool restoreRTC()
  {
  uint8_t persistedValue;
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    {
    // Restore the persisted days, though ignore if apparently unset (all 1s).
    const uint16_t days = eeprom_read_word((uint16_t*)V0P2BASE_EE_START_RTC_DAY_PERSIST);
    if(days != (uint16_t)~0U) { _daysSince1999LT = days; }

    // Now recover persisted HH:MM value.
    persistedValue = eeprom_read_byte((uint8_t*)V0P2BASE_EE_START_RTC_HHMM_PERSIST);
    }

  // Abort if value clearly invalid, eg likely an unprogrammed (0xff) byte.
  if(persistedValue >= (24 << 3)) { return(false); }

  uint_least16_t minutesSinceMidnight = (persistedValue >> 3) * 60;
  minutesSinceMidnight += 8; // Start just over half-way into one quantum to minimise average time lost on restart.
  const uint8_t lowBits = persistedValue & 7; // Extract quarter-hour bits.
  switch(lowBits)
    {
    case 0: minutesSinceMidnight += 45; break;
    case 1: minutesSinceMidnight += 30; break;
    case 3: minutesSinceMidnight += 15; break;
    case 7: break; // Nothing to add, but a valid 0-quarter bit pattern.
    default: return(false); // Invalid bit pattern: abort.
    }

  // Set the hours and minutes (atomically).
  // Deliberately leave the seconds unset to avoid units becoming too synchronised with one another, increasing TX collisions, etc.
  ATOMIC_BLOCK (ATOMIC_RESTORESTATE)
    { _minutesSinceMidnightLT = minutesSinceMidnight; }

  return(true);
  }


// Get minutes since midnight local time [0,1439].
// Useful to fetch time atomically for scheduling purposes.
// Preserves interrupt state.
// Thread-safe and ISR-safe.
#ifndef getMinutesSinceMidnightLT
uint_least16_t getMinutesSinceMidnightLT()
  {
  uint_least16_t result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    { result = _minutesSinceMidnightLT; }
  return(result);
  }
#endif

// Get local time minutes from RTC [0,59].
// Relatively slow.
// Thread-safe and ISR-safe.
uint_least8_t getMinutesLT() { return(getMinutesSinceMidnightLT() % 60); }

// Get local time hours from RTC [0,23].
// Relatively slow.
// Thread-safe and ISR-safe.
uint_least8_t getHoursLT() { return(getMinutesSinceMidnightLT() / 60); }

// Get whole days since the start of 2000/01/01 (ie the midnight between 1999 and 2000), local time.
// This will roll in about 2179, by which time I will not care.
// Thread-safe and ISR-safe.
uint_least16_t getDaysSince1999LT()
  {
  uint_least16_t result;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    { result = _daysSince1999LT; }
  return(result);
  }


// Get previous hour in current local time, wrapping round from 0 to 23.
uint_least8_t getPrevHourLT()
  {
  const uint_least16_t h = OTV0P2BASE::getHoursLT();
  if(0 == h) { return(23); }
  return(h - 1);
  }
// Get next hour in current local time, wrapping round from 23 back to 0.
uint_least8_t getNextHourLT()
  {
  const uint_least16_t h = OTV0P2BASE::getHoursLT();
  if(h >= 23) { return(0); }
  return(h + 1);
  }


// Set time as hours [0,23] and minutes [0,59].
// Will ignore attempts to set bad values and return false in that case.
// Returns true if all OK and the time has been set.
// Does not attempt to set seconds.
// Thread/interrupt safe, but do not call this from an ISR.
// Will persist time to survive reset / power-cycle as necessary.
bool setHoursMinutesLT(const uint8_t hours, const uint8_t minutes)
  {
  if((hours > 23) || (minutes > 59)) { return(false); } // Invalid time.
  const uint_least16_t computedMinutesSinceMidnightLT = (uint_least16_t) ((60 * (uint_least16_t)hours) + minutes);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
    if(computedMinutesSinceMidnightLT != _minutesSinceMidnightLT)
      {
      // If time has changed then store it locally and persist it if need be.
      _minutesSinceMidnightLT = computedMinutesSinceMidnightLT;
      persistRTC();
      }
    }
  return(true); // Assume set and persisted OK.
  }

// Set nominal seconds [0,59].
// Not persisted, may be offset from real time.
// Will ignore attempts to set bad values and return false in that case.
// Will drop the least significant bit if counting in 2s increments.
// Returns true if all OK and the time has been set.
// Thread/interrupt safe, but do not call this from an ISR.
bool setSeconds(const uint8_t seconds)
  {
  if(seconds > 59) { return(false); } // Invalid time.
#if defined(V0P2BASE_TWO_S_TICK_RTC_SUPPORT)
  _secondsLT = seconds & ~1; // Drop the bottom bit.  Assumed safe/atomic.
#else
  _secondsLT = seconds; // Assumed safe/atomic.
#endif
  return(true); // Assume set OK.
  }




// RTC-based watchdog, if enabled with enableRTCWatchdog(true),
// will force a reset if the resetRTCWatchDog() is not called
// between one RTC tick interrupt and the next.
//
// If true then the RTC-based watchdog mechanism is enabled.
static volatile bool _RTCWatchdogEnabled;
// If true, then enable the RTC-based watchdog; disable otherwise.
void enableRTCWatchdog(const bool enable) { _RTCWatchdogEnabled = enable; }
// If true on following tick, watchdog reset is triggered.
static volatile bool _RTCWatchdogResetNotCalled;
// Must be called between each 'tick' of the RTC clock if enabled, else system will reset.
void resetRTCWatchDog() { _RTCWatchdogResetNotCalled = false; }



// Hook into TIMER2 overflow interrupt to drive RTC and provide slow wake-up tick.
ISR(TIMER2_OVF_vect)
  {
  // Maintain RTC, as energy-efficiently as reasonably possible.
  // An ISR so must not:
  //   * do anything expensive,
  //   * access EEPROM,
  //   * enable interrupts,
  //   * alter interrupt state from how it was on entry.
  register uint_fast8_t sTemp = _secondsLT; // Avoid some redundant memory accesses.
  sTemp += MAIN_TICK_S;
  if(sTemp > 59)
    {
    sTemp = 0; // Seconds roll.
    register uint_least16_t mTemp = _minutesSinceMidnightLT + 1; // Avoid some redundant memory accesses.
    if(mTemp > 1439)
      {
      mTemp = 0; // Minutes/hours roll.
      // Increment the day.
      ++_daysSince1999LT; // Don't currently prevent roll.
      }
    _minutesSinceMidnightLT = mTemp;
    }
  _secondsLT = sTemp;
  // Deal with watchdog, if enabled.
  if(_RTCWatchdogEnabled)
    {
    if(_RTCWatchdogResetNotCalled) { forceReset(); }
    _RTCWatchdogResetNotCalled = true;
    }
  }


}
