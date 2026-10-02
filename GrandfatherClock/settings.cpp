/* =====================================================================
   settings.cpp  —  Minimal JSON persistence on LittleFS (no ArduinoJson)
   ---------------------------------------------------------------------
   The config file is small and written only by us, so we use a tiny
   hand-rolled reader/writer instead of pulling in ArduinoJson (keeps
   heap pressure low on the ESP8266). Format example:

   {
     "ssid":"ByteMe2",
     "pass":"kilobyte",
     "threshold":0.1800,
     "refractoryMs":1200,
     "tzOffset":-8.00,
     "tz":"PST8PDT,M3.2.0,M11.1.0"
   }
   ===================================================================== */
#include "settings.h"
#include "config.h"
#include "storage.h"

Settings settings;

static void applyDefaults() {
  settings.wifiSsid      = "";
  settings.wifiPass      = "";
  settings.threshold     = DEF_THRESHOLD;
  settings.refractoryMs  = DEF_REFRACTORY_MS;
  settings.tzOffsetHours = DEF_TZ_OFFSET;
  settings.tz            = DEF_TZ;
  settings.toneEnabled   = 0;
  settings.toneF1        = 0;
  settings.toneF2        = 0;
  settings.toneRatio     = DEF_TONE_RATIO;
  settings.halfHourStrike = 0;
  settings.windDays       = DEF_WIND_DAYS;
  settings.tickEnabled    = 0;
  settings.tickNominal    = 0;
}

// ---- tiny extractors -------------------------------------------------
// Find "key": and return the raw value text up to , or } (no nesting).
static bool findValue(const String& src, const char* key, String& out) {
  String pat = String('"') + key + "\"";
  int k = src.indexOf(pat);
  if (k < 0) return false;
  int colon = src.indexOf(':', k + pat.length());
  if (colon < 0) return false;
  int i = colon + 1;
  while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t')) i++;
  if (i >= (int)src.length()) return false;

  if (src[i] == '"') {                 // string value
    int end = i + 1;
    String v;
    while (end < (int)src.length() && src[end] != '"') {
      if (src[end] == '\\' && end + 1 < (int)src.length()) end++; // skip esc
      v += src[end++];
    }
    out = v;
  } else {                              // number / literal
    int end = i;
    while (end < (int)src.length() &&
           src[end] != ',' && src[end] != '}' && src[end] != '\n') end++;
    out = src.substring(i, end);
    out.trim();
  }
  return true;
}

static String jsonEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o;
}

bool tzValid(const String& tz) {
  if (tz.length() < 3 || tz.length() > 47) return false;
  for (size_t i = 0; i < tz.length(); i++) {
    char c = tz[i];
    if (!isalnum((unsigned char)c) && !strchr("<>+-,.:/", c)) return false;
  }
  return true;
}

String tzFromOffset(float hours) {
  if (fabsf(hours - DEF_TZ_OFFSET) < 0.01f) return DEF_TZ;   // old default
  int m = (int)lroundf(-hours * 60.0f);   // POSIX offsets are west-positive
  int am = m < 0 ? -m : m;
  char b[24];
  snprintf(b, sizeof(b), "UTC%s%d:%02d", m < 0 ? "-" : "", am / 60, am % 60);
  return String(b);
}

void loadSettings() {
  applyDefaults();
  if (!storageReady()) return;

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;

  String src;
  src.reserve(256);
  while (f.available()) src += (char)f.read();
  f.close();

  String v;
  if (findValue(src, "ssid", v))         settings.wifiSsid      = v;
  if (findValue(src, "pass", v))         settings.wifiPass      = v;
  if (findValue(src, "threshold", v))    settings.threshold     = v.toFloat();
  if (findValue(src, "refractoryMs", v)) settings.refractoryMs  = (uint32_t)v.toInt();
  if (findValue(src, "tzOffset", v))     settings.tzOffsetHours = v.toFloat();
  // Configs from before 2.16.1 have only the fixed offset: migrate it.
  if (findValue(src, "tz", v) && tzValid(v)) settings.tz = v;
  else                                       settings.tz = tzFromOffset(settings.tzOffsetHours);
  if (findValue(src, "toneEnabled", v))  settings.toneEnabled   = (uint8_t)v.toInt();
  if (findValue(src, "toneF1", v))       settings.toneF1        = v.toFloat();
  if (findValue(src, "toneF2", v))       settings.toneF2        = v.toFloat();
  if (findValue(src, "toneRatio", v))    settings.toneRatio     = v.toFloat();
  if (findValue(src, "halfHour", v))     settings.halfHourStrike = (uint8_t)v.toInt();
  if (findValue(src, "windDays", v))     settings.windDays      = (uint16_t)v.toInt();
  if (findValue(src, "tickEnabled", v))  settings.tickEnabled   = (uint8_t)v.toInt();
  if (findValue(src, "tickNominal", v))  settings.tickNominal   = v.toFloat();
}

bool saveSettings() {
  if (!storageReady()) return false;

  // "w" truncates, so no need to remove the old file first.
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;

  f.print(F("{\n"));
  f.print(F("  \"ssid\":\""));         f.print(jsonEscape(settings.wifiSsid)); f.print(F("\",\n"));
  f.print(F("  \"pass\":\""));         f.print(jsonEscape(settings.wifiPass)); f.print(F("\",\n"));
  f.print(F("  \"threshold\":"));      f.print(settings.threshold, 4);         f.print(F(",\n"));
  f.print(F("  \"refractoryMs\":"));   f.print(settings.refractoryMs);         f.print(F(",\n"));
  f.print(F("  \"tzOffset\":"));       f.print(settings.tzOffsetHours, 2);     f.print(F(",\n"));
  f.print(F("  \"tz\":\""));           f.print(settings.tz);                   f.print(F("\",\n"));
  f.print(F("  \"toneEnabled\":"));    f.print(settings.toneEnabled);          f.print(F(",\n"));
  f.print(F("  \"toneF1\":"));         f.print(settings.toneF1, 1);            f.print(F(",\n"));
  f.print(F("  \"toneF2\":"));         f.print(settings.toneF2, 1);            f.print(F(",\n"));
  f.print(F("  \"toneRatio\":"));      f.print(settings.toneRatio, 3);         f.print(F(",\n"));
  f.print(F("  \"halfHour\":"));       f.print(settings.halfHourStrike);       f.print(F(",\n"));
  f.print(F("  \"windDays\":"));       f.print(settings.windDays);             f.print(F(",\n"));
  f.print(F("  \"tickEnabled\":"));    f.print(settings.tickEnabled);          f.print(F(",\n"));
  f.print(F("  \"tickNominal\":"));    f.print(settings.tickNominal, 4);       f.print(F("\n"));
  f.print(F("}\n"));
  f.close();
  return true;
}
