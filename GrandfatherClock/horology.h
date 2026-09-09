/* =====================================================================
   horology.h  —  Hour-strike analysis: offset, drift rate, advisor
   ===================================================================== */
#ifndef HOROLOGY_H
#define HOROLOGY_H

#include <Arduino.h>

struct HoroStatus {
  // last completed hourly strike event
  uint32_t lastEpoch;      // epoch sec of first strike (0 = none yet)
  uint8_t  lastCount;      // strikes heard
  uint8_t  lastExpected;   // hour it should have struck
  float    lastOffset;     // seconds vs top of hour (+ = late)
  bool     lastValid;      // count matched & within window
  // regression since last adjustment
  float    rate;           // drift, s/day (+ = clock running slow -> late)
  bool     rateValid;
  uint16_t nMeas;          // valid measurements in current window
  // advisor
  float    k;              // learned sensitivity, s/day per turn
  bool     kValid;
  float    predTurns;      // recommended next adjustment
  bool     predValid;
  uint32_t lastAdjEpoch;
  float    lastAdjTurns;
  float    lastTempC;      // temperature at last measurement (-100 = none)
  // Phase B: stopped-clock alarm
  uint32_t lastStrikeEpoch;
  uint32_t secsSinceStrike;
  bool     stopped;
  // Phase C: wind reminder
  uint32_t lastWindEpoch;
  float    daysSinceWind;  // -1 if never logged
  bool     windDue;
};

void       horoBegin();                    // load logs from LittleFS
void       horoOnChime(uint64_t epochMs);  // feed each detected strike
void       horoUpdate();                   // call from loop(); closes events
void       horoLogAdjustment(float turns); // user turned the rating nut
void       horoLogWind();                  // user wound the clock (Phase C)
HoroStatus horoGetStatus();

// True while a strike sequence is in progress (strikes heard, gap not yet
// elapsed). Callers use this to avoid starting long blocking operations
// that would deafen the detector mid-sequence.
bool       horoEventOpen();

// Remove a single logged measurement (by epoch) and reload from flash.
bool       horoDeleteMeasurement(uint32_t epoch);

// Inputs pushed from the main loop to keep this module hardware-agnostic:
void       horoSetTemperature(float c);    // latest BME280 temp (Phase A)
void       horoSetHalfHour(bool en);       // half-hour strike mode (Phase D)
void       horoSetWindDays(uint16_t d);    // wind reminder interval (Phase C)

#endif // HOROLOGY_H
