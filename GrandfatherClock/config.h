/* =====================================================================
   config.h  —  Board pinout, AP/network constants, tunable defaults
   =====================================================================

   WIRING (FORIOT ESP8266 NodeMCU + built-in SSD1306 OLED)
   -------------------------------------------------------
   OLED (already wired internally on this board):
       SDA = GPIO14 (D5)      SCL = GPIO12 (D6)   [verified by I2C scan]
       -> Do NOT reuse D5/D6 for anything else.

   LM393 analog mic:
       OUT (AO) -> A0
       VCC      -> 3V3  (recommended over 5V to keep A0 in range)
       GND      -> GND

   That's the whole wiring list — storage is the onboard flash
   (LittleFS), so no SD reader and no SPI pins are used. D0, D1, D2,
   D3, D4, D7, D8 all remain free for future peripherals.

   ** A0 OVER-VOLTAGE NOTE **
   ESP8266 A0 tolerates 0–1.0V at the chip. Many NodeMCU boards add an
   internal divider so the labeled A0 pin accepts up to ~3.3V. If yours
   does NOT, add: AO --[10k]--+--[22k]-- GND, tap the junction to A0.
   Power the mic from 3V3 to lower its bias. Calibrate threshold live.

   ** IDE FLASH LAYOUT **
   Select Tools -> Flash Size -> a layout WITH filesystem space, e.g.
   "4MB (FS:2MB)". With FS:none, settings and the chime log cannot work.
   ===================================================================== */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// ---------- Firmware version (semver) ----------
// MAJOR: breaking hardware/storage changes; MINOR: features; PATCH: fixes
//   0.1.0 Rev010 MPU6050 | 0.2.0 Rev011 LM393/D1 Mini
//   1.0.0 Rev012 FORIOT+SD | 1.0.1 review fixes
//   2.0.0 SD->LittleFS | 2.1.0 mDNS+OTA+pass-2 fixes | 2.1.1 OLED pins swapped | 2.1.2 AP stability fixes | 2.2.0 scope streaming | 2.3.0 Y scale + 30s window | 2.4.0 scope polish | 2.5.0 chime learning + horology | 2.5.1 f2 gate fix | 2.6.0 env/half-hour/alarm/wind | 2.7.0 tick analysis + timegrapher | 2.7.1 UI redesign | 2.7.2 wrap-safe tick sampling | 2.8.0 help mode + mic gain tool | 2.8.1 review fixes | 2.9.0 Brass & Walnut theme | 2.10.0 feedback round | 2.10.1 chime-log download fix + logstat | 2.10.2 CSV date/time split | 2.11.0 gettimeofday timing fix + sec_from_hour columns | 2.11.1 log-scale default | 2.12.0 xlsx export | 2.13.0 1-hour history | 2.14.0 chime peak fix | 2.14.1 DRAM saving | 2.15.0 browser OTA at /update (bypasses the IDE espota/mDNS bug)
#define FW_VERSION "2.15.0"

// ---------- SoftAP provisioning ----------
#define AP_SSID  "GrandfatherClock-Setup"
#define AP_PASS  ""                 // "" = open network; set 8+ chars to lock
static const IPAddress AP_IP(192, 168, 4, 1);

// ---------- WiFi station ----------
#define WIFI_CONNECT_TIMEOUT_MS  15000UL

// ---- Optional static IP (uncomment + fill to enable) ----
// #define USE_STATIC_IP
#ifdef USE_STATIC_IP
  static const IPAddress STATIC_IP     (192, 168, 1, 50);
  static const IPAddress STATIC_GATEWAY(192, 168, 1, 1);
  static const IPAddress STATIC_SUBNET (255, 255, 255, 0);
  static const IPAddress STATIC_DNS    (192, 168, 1, 1);
#endif

// ---------- Pins ----------
#define PIN_MIC_AO   A0

// OLED I2C (built in) — for reference; Wire pins set in display.cpp
#define PIN_OLED_SDA 14   // D5  (scanner-verified; Amazon listing had these swapped)
#define PIN_OLED_SCL 12   // D6
#define OLED_ADDR    0x3C
#define OLED_W       128
#define OLED_H        64

// ---------- Identity / OTA ----------
#define MDNS_HOSTNAME "grandfatherclock"   // http://grandfatherclock.local
// #define OTA_PASSWORD "change-me"        // uncomment to password OTA flashes

// ---------- Storage paths (LittleFS) ----------
#define CONFIG_PATH          "/config.json"
#define CHIME_LOG_PATH       "/chimes.csv"
#define CHIME_LOG_OLD_PATH   "/chimes.old.csv"
#define CHIME_LOG_MAX_BYTES  131072UL   // rotate at 128KB (~7k chime rows)
#define DRIFT_LOG_PATH       "/drift.csv"     // epoch,count,expected,offset,valid
#define ADJUST_LOG_PATH      "/adjust.csv"    // epoch,turns,rateBefore
#define WIND_LOG_PATH        "/wind.csv"      // epoch (each winding logged)
#define DRIFT_TMP_PATH       "/drift.tmp"     // scratch for row deletion

// ---------- Time ----------
#define NTP_MIN_EPOCH   1700000000UL   // reject garbage NTP (before ~Nov 2023)

// ---------- Sound detection defaults ----------
#define DEF_THRESHOLD     0.18f   // envelope above ambient to call a chime (0..1)
#define DEF_REFRACTORY_MS 1200u   // ignore window after a chime
#define DEF_TZ_OFFSET    -8.0f    // Pacific
#define DEF_TONE_RATIO    0.20f   // learned-tone energy fraction to accept
#define STRIKE_GAP_MS     6000u   // silence that closes a strike event
// After a strike triggers, keep tracking the envelope for this long and
// report the MAXIMUM as the chime's peak. Reporting the value at threshold
// crossing (the rising edge) made every chime read back at roughly the
// threshold, so loudness differences were invisible and gain could not be set.
#define CHIME_PEAK_WINDOW_MS 400u
#define ENV_POLL_MS       30000u  // BME280 sample cadence (slow signals)

// --- Phase B: clock-stopped detection ---
#define STOP_ALARM_MS     4500000UL  // 75 min of silence -> clock stopped
// (one hour + 15 min grace; half-hour mode tightens effective cadence)

// --- Phase C: wind reminder / health ---
#define DEF_WIND_DAYS     7          // remind to wind after N days (0=off)
#define HEALTH_LOG_PATH   "/health.csv"   // daily amplitude/decay aggregates

// --- Phase E: escapement tick analysis ---
#define TICK_SR            2000    // A0 sample rate during a tick window (Hz)
#define TICK_WINDOW_S      12.0f   // capture length per measurement
#define TICK_REFRACTORY_MS 250     // min gap between onsets (< half-beat)
#define TICK_ONSET_K       1.8f    // onset threshold = floor*K + MIN
#define TICK_ONSET_MIN     0.010f
#define TICK_LOG_PATH      "/tick.csv"      // epoch,beat,rate,beatErr aggregates
#define TICK_LOG_MAX_BYTES 131072UL
#define TICK_CAPTURE_INTERVAL_MS 300000UL   // one tick capture every 5 min
// Don't start a (blocking) tick capture within this many seconds of a
// strike boundary — a 12-strike train plus gap runs ~30s, and missing
// strikes would invalidate the hourly drift measurement.
#define TICK_STRIKE_GUARD_S      90L
#define SAMPLE_INTERVAL_US 2000   // ~500 Hz

// ---------- Scope trace streaming ----------
#define SCOPE_BIN_MS     25    // fold 500Hz samples into 25ms peak bins
#define SCOPE_RING       32    // bins kept firmware-side
#define SCOPE_SEND_BINS  32    // bins per /api/state response (covers 800ms)

// --- Long-term history (device side) ---
// The 25ms scope stream lives only in the browser and dies on refresh, so
// long windows would start empty. The device also keeps a coarse ring of
// one peak per second, which survives page reloads and can be fetched whole.
// 3600 entries x uint16 = 7.2 KB static (BSS, not heap).
#define HIST_SECONDS     3600  // 1 hour of 1-second peak bins
#define HIST_SCALE       10000 // stored as uint16: excess * 10000, clamped

#endif // CONFIG_H
