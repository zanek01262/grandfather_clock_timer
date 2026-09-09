/* =====================================================================
   horology.cpp  —  Turning strike timestamps into pendulum guidance
   ---------------------------------------------------------------------
   Pipeline:
     strikes -> group into events (gap > STRIKE_GAP_MS closes one)
             -> count == hour? within +/-10min of a top-of-hour? -> valid
             -> offset = first strike vs nearest hour, milliseconds
             -> linear regression of offset vs time since the last
                adjustment  ->  drift rate in s/day
     adjustments -> each logs (epoch, turns, rateBefore); comparing the
                rate before an adjustment with the rate that follows it
                yields k = s/day per turn. Recommendation: -rate/k.

   Persistence: /drift.csv   epoch,count,expected,offset,valid
                /adjust.csv  epoch,turns,rateBefore
   Both reloaded (tail into RAM rings) at boot, so reboots/OTA don't
   lose the measurement window.
   ===================================================================== */
#include "horology.h"
#include "config.h"
#include "storage.h"
#include <time.h>

#define MEAS_RING 64
#define ADJ_RING   8

struct Meas { uint32_t epoch; uint16_t ms; float offset; uint8_t count, expected, valid; float tempC; uint8_t isHalf; };
struct Adj  { uint32_t epoch; float turns; float rateBefore; };

static Meas s_meas[MEAS_RING]; static uint16_t s_measN = 0;   // total ever
static Adj  s_adj[ADJ_RING];   static uint16_t s_adjN  = 0;

static uint32_t s_lastStrikeEpoch = 0;   // any strike, for stopped-clock alarm
static uint32_t s_lastWindEpoch   = 0;   // last logged winding (Phase C)

// Current temperature, pushed in from the main loop (keeps this module
// free of any sensor-library dependency so it stays host-testable).
static float g_tempC = -100.0f;   // sentinel: no reading
void horoSetTemperature(float c) { g_tempC = c; }

// open event state
static uint64_t evFirstMs = 0;
static uint32_t evLastMs  = 0;
static uint8_t  evCount   = 0;

static void pushMeas(const Meas& m) { s_meas[s_measN % MEAS_RING] = m; s_measN++; }
static void pushAdj (const Adj& a)  { s_adj [s_adjN  % ADJ_RING ] = a; s_adjN++;  }

// ---------------------------------------------------------------------
static void loadLogs() {
  if (!storageReady()) return;
  File f = LittleFS.open(DRIFT_LOG_PATH, "r");
  if (f) {
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      Meas m; unsigned long e; unsigned c, x, v; float o;
      float tc = -100.0f; unsigned hh = 0, msv = 0;
      int got = sscanf(ln.c_str(), "%lu,%u,%u,%f,%u,%f,%u,%u",
                       &e, &c, &x, &o, &v, &tc, &hh, &msv);
      if (got >= 5) {
        m.epoch = e; m.count = c; m.expected = x; m.offset = o; m.valid = v;
        m.tempC  = (got >= 6) ? tc : -100.0f;
        m.isHalf = (got >= 7) ? (uint8_t)hh : 0;
        m.ms     = (got >= 8) ? (uint16_t)msv : 0;
        pushMeas(m);
      }
      yield();
    }
    f.close();
  }
  f = LittleFS.open(ADJUST_LOG_PATH, "r");
  if (f) {
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      Adj a; unsigned long e; float t, r;
      if (sscanf(ln.c_str(), "%lu,%f,%f", &e, &t, &r) == 3) {
        a.epoch = e; a.turns = t; a.rateBefore = r;
        pushAdj(a);
      }
      yield();
    }
    f.close();
  }
  Serial.printf("[HORO] loaded %u measurements, %u adjustments\n",
                s_measN, s_adjN);
}

static void loadWind() {
  if (!storageReady()) return;
  File f = LittleFS.open(WIND_LOG_PATH, "r");
  if (!f) return;
  while (f.available()) {
    String ln = f.readStringUntil('\n');
    unsigned long e;
    if (sscanf(ln.c_str(), "%lu", &e) == 1) s_lastWindEpoch = e;
    yield();
  }
  f.close();
}

void horoLogWind() {
  s_lastWindEpoch = (uint32_t)time(nullptr);
  if (storageReady()) {
    File f = LittleFS.open(WIND_LOG_PATH, "a");
    if (f) { f.printf("%lu\n", (unsigned long)s_lastWindEpoch); f.close(); }
  }
  Serial.println(F("[HORO] winding logged"));
}

void horoBegin() { loadLogs(); loadWind(); }

// ---------------------------------------------------------------------
static uint32_t lastAdjEpoch() {
  return s_adjN ? s_adj[(s_adjN - 1) % ADJ_RING].epoch : 0;
}

// Drift rate (s/day) via least-squares over valid measurements newer
// than `sinceEpoch`. Needs >=2 points spanning >=2 hours.
static bool regress(uint32_t sinceEpoch, float* rateOut, uint16_t* nOut) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  uint16_t n = 0;
  uint32_t tmin = 0xFFFFFFFF, tmax = 0;
  uint16_t avail = s_measN < MEAS_RING ? s_measN : MEAS_RING;
  uint32_t base = 0;
  for (uint16_t i = 0; i < avail; i++) {
    const Meas& m = s_meas[(s_measN - avail + i) % MEAS_RING];
    if (!m.valid || m.epoch <= sinceEpoch) continue;
    if (!base) base = m.epoch;
    double x = (double)(m.epoch - base) / 86400.0;   // days
    double y = m.offset;
    sx += x; sy += y; sxx += x * x; sxy += x * y; n++;
    if (m.epoch < tmin) tmin = m.epoch;
    if (m.epoch > tmax) tmax = m.epoch;
  }
  *nOut = n;
  if (n < 2 || (tmax - tmin) < 7200) return false;
  double den = (double)n * sxx - sx * sx;
  if (fabs(den) < 1e-12) return false;
  *rateOut = (float)(((double)n * sxy - sx * sy) / den);   // s/day
  return true;
}

// Learned screw sensitivity: average of (rate change)/(turns) across
// adjustment boundaries, including the live window's rate if available.
static bool estimateK(float liveRate, bool liveValid, float* kOut) {
  uint16_t avail = s_adjN < ADJ_RING ? s_adjN : ADJ_RING;
  float sum = 0; int n = 0;
  for (uint16_t i = 0; i + 1 < avail; i++) {
    const Adj& a = s_adj[(s_adjN - avail + i)     % ADJ_RING];
    const Adj& b = s_adj[(s_adjN - avail + i + 1) % ADJ_RING];
    if (fabsf(a.turns) < 0.01f) continue;
    sum += (b.rateBefore - a.rateBefore) / a.turns; n++;
  }
  if (avail >= 1 && liveValid) {
    const Adj& last = s_adj[(s_adjN - 1) % ADJ_RING];
    if (fabsf(last.turns) >= 0.01f) {
      sum += (liveRate - last.rateBefore) / last.turns; n++;
    }
  }
  if (!n) return false;
  float k = sum / n;
  if (fabsf(k) < 0.5f) return false;   // implausibly insensitive: distrust
  *kOut = k;
  return true;
}

// ---------------------------------------------------------------------
// g_halfHourEnabled is set from settings by the caller each loop (keeps
// horology independent of the settings struct layout for host testing).
static bool g_halfHourEnabled = false;
static uint16_t g_windDays = 0;
void horoSetWindDays(uint16_t d) { g_windDays = d; }
void horoSetHalfHour(bool en) { g_halfHourEnabled = en; }

static void finalizeEvent() {
  uint8_t  count   = evCount;
  uint64_t firstMs = evFirstMs;
  evCount = 0;

  time_t first = (time_t)(firstMs / 1000ULL);

  // Distance to nearest top-of-hour and to nearest half-hour boundary.
  time_t nearestHour = ((first + 1800) / 3600) * 3600;
  float  offHour = (float)((int64_t)firstMs - (int64_t)nearestHour * 1000LL) / 1000.0f;

  time_t nearestHalf = ((first + 1800) / 3600) * 3600 + 1800;   // the :30 mark
  // pick whichever :30 (this hour's or previous) is closest
  if (first < nearestHalf - 1800) nearestHalf -= 3600;
  float  offHalf = (float)((int64_t)firstMs - (int64_t)nearestHalf * 1000LL) / 1000.0f;

  struct tm tmv;
  localtime_r(&nearestHour, &tmv);
  uint8_t hourExpected = tmv.tm_hour % 12;
  if (hourExpected == 0) hourExpected = 12;

  // Classify: a single strike close to :30 (and enabled) is a half-hour
  // strike; otherwise judge as an hour event.
  bool isHalf = false;
  uint8_t expected; float offset;
  if (g_halfHourEnabled && count == 1 && fabsf(offHalf) < fabsf(offHour)) {
    isHalf = true; expected = 1; offset = offHalf;
  } else {
    expected = hourExpected; offset = offHour;
  }

  bool valid = (count == expected) && (fabsf(offset) < 600.0f);

  Meas m;
  m.epoch = (uint32_t)first;
  m.ms = (uint16_t)(firstMs % 1000ULL);
  m.offset = offset;
  m.count = count; m.expected = expected; m.valid = (uint8_t)valid;
  m.tempC = g_tempC; m.isHalf = isHalf ? 1 : 0;
  pushMeas(m);
  if (storageReady()) {
    File f = LittleFS.open(DRIFT_LOG_PATH, "a");
    if (f) {
      f.printf("%lu,%u,%u,%.3f,%u,%.2f,%u,%u\n",
               (unsigned long)first, count, expected, offset, valid ? 1 : 0,
               g_tempC, isHalf ? 1 : 0, (unsigned)(firstMs % 1000ULL));
      f.close();
    }
  }
  Serial.printf("[HORO] event: %u strikes (expected %u%s) offset %+.1fs %s\n",
                count, expected, isHalf ? " :30" : "", offset,
                valid ? "VALID" : "ignored");
}

void horoOnChime(uint64_t epochMs) {
  if (epochMs == 0) return;              // no NTP yet: can't place in time
  s_lastStrikeEpoch = (uint32_t)(epochMs / 1000ULL);
  uint32_t now = millis();
  if (evCount > 0 && (uint32_t)(now - evLastMs) > STRIKE_GAP_MS) finalizeEvent();
  if (evCount == 0) evFirstMs = epochMs;
  evCount++;
  evLastMs = now;
}

void horoUpdate() {
  if (evCount > 0 && (uint32_t)(millis() - evLastMs) > STRIKE_GAP_MS)
    finalizeEvent();
}

bool horoEventOpen() { return evCount > 0; }

// Delete one measurement row by its epoch and reload the RAM ring. A single
// mis-detected strike event can visibly skew the drift regression, so the
// user needs a way to drop it without wiping the whole history.
bool horoDeleteMeasurement(uint32_t epoch) {
  if (!storageReady()) return false;
  File in = LittleFS.open(DRIFT_LOG_PATH, "r");
  if (!in) return false;
  File out = LittleFS.open(DRIFT_TMP_PATH, "w");
  if (!out) { in.close(); return false; }

  bool removed = false;
  while (in.available()) {
    String ln = in.readStringUntil('\n');
    if (ln.length() == 0) continue;
    unsigned long e = 0;
    if (sscanf(ln.c_str(), "%lu", &e) == 1 && (uint32_t)e == epoch && !removed) {
      removed = true;                 // drop only the first match
      yield();
      continue;
    }
    out.print(ln); out.print("\n");
    yield();
  }
  in.close(); out.close();

  if (removed) {
    LittleFS.remove(DRIFT_LOG_PATH);
    LittleFS.rename(DRIFT_TMP_PATH, DRIFT_LOG_PATH);
    s_measN = 0;                      // rebuild the ring from the new file
    loadLogs();
    Serial.printf("[HORO] measurement %lu deleted\n", (unsigned long)epoch);
  } else {
    LittleFS.remove(DRIFT_TMP_PATH);
  }
  return removed;
}

void horoLogAdjustment(float turns) {
  float rate = 0; uint16_t n = 0;
  bool rv = regress(lastAdjEpoch(), &rate, &n);
  Adj a = { (uint32_t)time(nullptr), turns, rv ? rate : 0 };
  pushAdj(a);
  if (storageReady()) {
    File f = LittleFS.open(ADJUST_LOG_PATH, "a");
    if (f) {
      f.printf("%lu,%.3f,%.3f\n", (unsigned long)a.epoch, turns, a.rateBefore);
      f.close();
    }
  }
  Serial.printf("[HORO] adjustment logged: %+.2f turns (rate before %+.2f s/day)\n",
                turns, a.rateBefore);
}

HoroStatus horoGetStatus() {
  HoroStatus h = {};
  uint16_t avail = s_measN < MEAS_RING ? s_measN : MEAS_RING;
  if (avail) {
    const Meas& m = s_meas[(s_measN - 1) % MEAS_RING];
    h.lastEpoch = m.epoch;   h.lastCount = m.count;
    h.lastExpected = m.expected; h.lastOffset = m.offset;
    h.lastValid = m.valid;
  }
  h.rateValid = regress(lastAdjEpoch(), &h.rate, &h.nMeas);
  h.kValid    = estimateK(h.rate, h.rateValid, &h.k);
  h.predValid = h.rateValid && h.kValid;
  if (h.predValid) h.predTurns = -h.rate / h.k;
  if (s_adjN) {
    const Adj& a = s_adj[(s_adjN - 1) % ADJ_RING];
    h.lastAdjEpoch = a.epoch; h.lastAdjTurns = a.turns;
  }

  // last measurement's temperature
  {
    uint16_t avail = s_measN < MEAS_RING ? s_measN : MEAS_RING;
    if (avail) h.lastTempC = s_meas[(s_measN - 1) % MEAS_RING].tempC;
    else h.lastTempC = -100.0f;
  }

  // Phase B: stopped-clock. Compare wall clock to last heard strike.
  uint32_t nowEp = (uint32_t)time(nullptr);
  h.lastStrikeEpoch = s_lastStrikeEpoch;
  h.secsSinceStrike = (s_lastStrikeEpoch && nowEp >= s_lastStrikeEpoch)
                      ? (nowEp - s_lastStrikeEpoch) : 0;
  // Compare in SECONDS, never milliseconds. `secsSinceStrike * 1000` is a
  // 32-bit multiply on the ESP8266 (unsigned long is 32-bit there, unlike
  // the 64-bit host used for testing) and wraps at ~49.7 days of silence,
  // which would silently CLEAR the alarm on a long-stopped clock.
  h.stopped = (s_lastStrikeEpoch != 0) &&
              (h.secsSinceStrike > (uint32_t)(STOP_ALARM_MS / 1000UL));

  // Phase C: wind reminder.
  h.lastWindEpoch = s_lastWindEpoch;
  h.daysSinceWind = (s_lastWindEpoch && nowEp >= s_lastWindEpoch)
                    ? (float)(nowEp - s_lastWindEpoch) / 86400.0f : -1.0f;
  h.windDue = (g_windDays > 0) && (h.daysSinceWind >= (float)g_windDays);

  return h;
}
