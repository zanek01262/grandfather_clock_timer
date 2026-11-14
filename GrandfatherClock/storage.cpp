/* =====================================================================
   storage.cpp  —  LittleFS storage (settings + chime log) on flash
   ---------------------------------------------------------------------
   Notes:
     * LittleFS.begin() on the ESP8266 auto-formats an unformatted
       partition by default, so first boot "just works".
     * Chime writes are rare (a clock chimes dozens of times a day, not
       thousands), so flash wear is a non-issue.
     * Log rotation: when /chimes.csv exceeds CHIME_LOG_MAX_BYTES it is
       renamed to /chimes.old.csv (replacing any previous one) and a
       fresh file starts. Worst case on disk: 2x the cap.
   ===================================================================== */
#include "storage.h"
#include "config.h"
#include <time.h>

static bool g_ready = false;

void storageBegin() {
  g_ready = LittleFS.begin();
  if (g_ready) {
    FSInfo info;
    LittleFS.info(info);
    Serial.printf("[FS] mounted: %u / %u bytes used\n",
                  (unsigned)info.usedBytes, (unsigned)info.totalBytes);
  } else {
    Serial.println(F("[FS] mount failed — check Tools>Flash Size has FS"));
  }
}

bool storageReady() { return g_ready; }

void formatLocalParts(uint64_t epochMs, char* date, size_t dn,
                      char* timeOut, size_t tn) {
  time_t secs = (time_t)(epochMs / 1000ULL);
  unsigned ms = (unsigned)(epochMs % 1000ULL);
  struct tm tmv;
  localtime_r(&secs, &tmv);
  strftime(date, dn, "%Y-%m-%d", &tmv);
  char hms[12];
  strftime(hms, sizeof(hms), "%H:%M:%S", &tmv);
  snprintf(timeOut, tn, "%s.%03u", hms, ms);
}

void logChime(uint64_t epochMs, float peak) {
  if (!g_ready) return;

  File f = LittleFS.open(CHIME_LOG_PATH, "a");
  if (!f) return;
  // %llu is unavailable on some ESP8266 cores: split into s + ms.
  f.printf("%lu%03u,%.4f\n",
           (unsigned long)(epochMs / 1000ULL),
           (unsigned)(epochMs % 1000ULL), peak);
  size_t sz = f.size();
  f.close();

  if (sz > CHIME_LOG_MAX_BYTES) {
    LittleFS.remove(CHIME_LOG_OLD_PATH);          // ok if absent
    LittleFS.rename(CHIME_LOG_PATH, CHIME_LOG_OLD_PATH);
    Serial.println(F("[FS] chime log rotated"));
  }
}

bool clearChimeLog(size_t* freedBytes) {
  // The chime log, plus files left behind by v2 firmware: v3 never reads
  // them and has no way to download them, so they are dead flash space.
  static const char* const parts[] = {
    CHIME_LOG_PATH, CHIME_LOG_OLD_PATH,
    "/drift.csv", "/adjust.csv", "/wind.csv", "/tick.csv", "/tick.csv.old"
  };
  size_t freed = 0;
  if (g_ready) {
    for (const char* path : parts) {
      File f = LittleFS.open(path, "r");
      if (f) { freed += f.size(); f.close(); }
      LittleFS.remove(path);                      // ok if absent
    }
  }
  if (freedBytes) *freedBytes = freed;
  bool ok = g_ready && !LittleFS.exists(CHIME_LOG_PATH)
                    && !LittleFS.exists(CHIME_LOG_OLD_PATH);
  Serial.printf("[FS] chime log cleared (%u bytes) %s\n",
                (unsigned)freed, ok ? "ok" : "FAILED");
  return ok;
}
