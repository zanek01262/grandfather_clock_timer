/* =====================================================================
   settings.h  —  Persistent settings (JSON on LittleFS flash)
   ===================================================================== */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

struct Settings {
  String   wifiSsid;
  String   wifiPass;
  float    threshold;       // chime envelope threshold (0..1 above ambient)
  uint32_t refractoryMs;    // dead time after a detected chime
  float    tzOffsetHours;   // e.g. -8.0 for Pacific
  uint8_t  toneEnabled;     // 1 = require learned-tone match to count chime
  float    toneF1, toneF2;  // learned chime frequencies (0 = unset)
  float    toneRatio;       // tone energy fraction needed to pass
  uint8_t  halfHourStrike;  // 1 = clock also strikes once at :30 (Phase D)
  uint16_t windDays;        // remind to wind after this many days (0=off)
  uint8_t  tickEnabled;     // 1 = escapement tick analysis active (Phase E)
  float    tickNominal;     // nominal half-beat seconds (0 = auto)
};

extern Settings settings;

void loadSettings();   // fills `settings` from config.json, or defaults
bool saveSettings();   // writes `settings` to config.json (true on success)

#endif // SETTINGS_H
