/* =====================================================================
   Grandfather Clock Tool  —  Rev012.1  (ESP8266 NodeMCU + OLED + mic)
   ---------------------------------------------------------------------
   Board : FORIOT ESP8266 NodeMCU w/ built-in 0.96" SSD1306 OLED (I2C)
   Mic   : LM393 analog module, AO -> A0 (see wiring notes in config.h)
   Store : onboard flash via LittleFS (no SD card in this build)

   IDE: select Tools -> Flash Size -> a layout WITH filesystem space,
        e.g. "4MB (FS:2MB)". FS:none disables settings + logging.

   Setup flow:
     1. On first boot (no saved WiFi creds), device starts a SoftAP
        named by AP_SSID and serves a splash/setup page at AP_IP.
     2. User connects phone/laptop to that AP, opens AP_IP in a browser,
        picks their network + enters password on a polished splash page.
     3. Device saves creds to flash (config.json), reboots, joins WiFi.
     4. From then on it runs in station mode: live dashboard + chime log.

   The OLED shows setup/WiFi status while provisioning, then flips to a
   live sound-level + last-chime view once connected.
   ===================================================================== */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPUpdateServer.h>   // browser-based OTA (core library)
#include <DNSServer.h>
#include <time.h>
#include <sys/time.h>

#include "config.h"
#include "settings.h"
#include "sound.h"
#include "display.h"
#include "storage.h"
#include "webpages.h"
#include "analysis.h"

ESP8266WebServer server(80);
// Browser OTA. ArduinoOTA (IDE network port) is kept, but the IDE 2.x has a
// long-standing bug where a network port's properties fail to resolve
// ("invalid integer value: '{upload.port.properties.port}'") whenever mDNS
// discovery drops the device. This path does not involve the IDE, espota or
// mDNS at all: browse to /update and upload the .bin.
ESP8266HTTPUpdateServer httpUpdater;
DNSServer        dnsServer;

// Runtime mode: are we provisioning (AP) or running (STA)?
bool   apMode          = false;
String scanResultsJson = "[]";   // cached network scan for the splash UI

// Deferred reboot: handlers set this instead of calling ESP.restart()
// directly, so the HTTP response fully flushes before we go down.
// (Hard-won lesson from a prior revision — don't block/reset in handlers.)
uint32_t g_rebootAt = 0;

// ---- NTP / time -----------------------------------------------------
static bool timeSynced = false;

static void startNTP() {
  // POSIX TZ rule, so daylight saving switches by itself. The old
  // configTime(offset, 0, ...) form has no DST rules: during PDT the chime
  // log's local times ran an hour behind.
  configTime(settings.tz.c_str(), "pool.ntp.org", "time.nist.gov");
}

// 64-bit epoch milliseconds, guarded against 32-bit overflow + garbage NTP.
unsigned long long epochMillis() {
  // Sub-second time MUST come from gettimeofday(), which SNTP disciplines.
  // The earlier version used `millis() % 1000`, but that is the millisecond
  // fraction of UPTIME and has no phase relationship to the NTP second
  // boundary — it was an arbitrary offset that drifted with the crystal
  // (~20-40 ppm, i.e. 70-150 ms of wander per hour), adding real noise to
  // every strike-offset measurement. Precise-looking digits, no accuracy.
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return 0ULL;
  if ((unsigned long)tv.tv_sec < NTP_MIN_EPOCH) return 0ULL;   // not synced
  return (unsigned long long)tv.tv_sec * 1000ULL +
         (unsigned long long)(tv.tv_usec / 1000);
}

bool timeIsValid() {
  return (unsigned long)time(nullptr) >= NTP_MIN_EPOCH;
}

// =====================================================================
//  WEB ROUTES — shared
// =====================================================================
static void handleRoot() {
  if (apMode) Serial.println(F("[HTTP] GET / (setup page served)"));
  if (apMode) server.send_P(200, "text/html", SETUP_PAGE);
  else        server.send_P(200, "text/html", DASH_PAGE);
}

// ---- AP / provisioning routes ---------------------------------------
static void buildScanJson(int n) {
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ssid\":\"";
    String s = WiFi.SSID(i);
    for (size_t k = 0; k < s.length(); k++) {
      char c = s[k];
      if (c == '"' || c == '\\') j += '\\';
      j += c;
    }
    j += "\",\"rssi\":" + String(WiFi.RSSI(i)) +
         ",\"lock\":" + String(WiFi.encryptionType(i) == ENC_TYPE_NONE ? 0 : 1) +
         "}";
  }
  j += "]";
  scanResultsJson = j;
}

static void handleScan() {
  // Never scan synchronously here: a blocking scan channel-hops the radio
  // and DROPS connected AP clients — the phone loading this very page.
  // Serve the cache; harvest a finished async rescan if one completed.
  int st = WiFi.scanComplete();
  if (st >= 0) { buildScanJson(st); WiFi.scanDelete(); }
  server.send(200, "application/json", scanResultsJson);
}

static void handleRescan() {
  WiFi.scanNetworks(true /*async*/);   // radio still hops; client may blip,
                                       // but the server never blocks
  server.send(200, "application/json", "{\"ok\":true}");
}

static void rescanNetworks() { buildScanJson(WiFi.scanNetworks()); }

static void handleSave() {
  if (!server.hasArg("ssid")) { server.send(400, "text/plain", "missing ssid"); return; }
  settings.wifiSsid = server.arg("ssid");
  settings.wifiPass = server.arg("pass");
  bool ok = saveSettings();
  // Respond before rebooting so the splash can show a success state.
  server.send(200, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"save failed\"}");
  if (ok) g_rebootAt = millis() + 1200;   // reboot from loop(), post-flush
}

// ---- STA / dashboard routes -----------------------------------------
static void handleState() {
  SoundState s = soundGetState();

  // Scope bins stream incrementally: the page sends ?since=<first bin it
  // still needs> and gets bins [scopeFrom, scopeFrom + scope.length). Sending
  // a fixed "last N bins" dropped everything that aged out between polls.
  uint32_t seq    = soundScopeSeq();
  uint32_t oldest = (seq > SCOPE_RING) ? seq - SCOPE_RING : 0;
  uint32_t from   = oldest;
  if (server.hasArg("since")) {
    uint32_t since = strtoul(server.arg("since").c_str(), nullptr, 10);
    // since > seq means the device rebooted under an open page: resend all.
    if (since > oldest && since <= seq) from = since;
  } else if (seq - oldest > SCOPE_SEND_BINS) {
    from = seq - SCOPE_SEND_BINS;   // cached pre-2.15.1 page: expects the newest bins
  }
  uint32_t to = from + SCOPE_SEND_BINS;
  if (to > seq) to = seq;

  // Fixed buffer, no String concat: this runs 5x/sec for months on end,
  // and repeated String churn slowly fragments the ESP8266 heap.
  // Worst case ~230 header + 64 bins x 7 chars = ~680.
  char buf[768];
  int off = snprintf(buf, sizeof(buf),
    "{\"level\":%.4f,\"ambient\":%.4f,\"peak\":%.4f,\"threshold\":%.4f,"
    "\"chimes\":%lu,\"lastChime\":%lu,\"lastPeak\":%.4f,\"timeValid\":%d,\"epoch\":%lu,"
    "\"fw\":\"" FW_VERSION "\",\"scopeFrom\":%lu,\"scopeSeq\":%lu,\"scope\":[",
    (double)s.level, (double)s.ambient, (double)s.peak,
    (double)settings.threshold,
    (unsigned long)s.chimeCount, (unsigned long)s.lastChimeEpoch,
    (double)s.lastChimePeak,
    timeIsValid() ? 1 : 0, (unsigned long)time(nullptr),
    (unsigned long)from, (unsigned long)seq);
  // 4 decimals, same as the chime log, so a logged peak and its plotted bin
  // agree (at %.3f a peak just over threshold could plot exactly on the line).
  // A truncated array is safe: the page re-requests from where it stopped.
  for (uint32_t b = from; b < to && off < (int)sizeof(buf) - 12; b++)
    off += snprintf(buf + off, sizeof(buf) - off, "%s%.4f",
                    b > from ? "," : "", (double)soundScopeAt(b));
  snprintf(buf + off, sizeof(buf) - off, "]}");
  server.send(200, "application/json", buf);
}

static void handleConfigGet() {
  // Device-local time, so the page can show whether the time zone is right.
  char lt[32] = "";
  if (timeIsValid()) {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(lt, sizeof(lt), "%Y-%m-%d %H:%M %Z", &tmv);
  }
  String j = "{";
  j += "\"threshold\":"   + String(settings.threshold, 4) + ",";
  j += "\"refractoryMs\":" + String(settings.refractoryMs) + ",";
  j += "\"tzOffset\":"    + String(settings.tzOffsetHours, 2) + ",";
  j += "\"tz\":\""        + settings.tz + "\",";      // tzValid(): no quotes
  j += "\"localTime\":\"" + String(lt) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

static void handleConfigSet() {
  // Validate before changing anything, so a bad request applies nothing.
  if (server.hasArg("tz") && !tzValid(server.arg("tz"))) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad tz\"}");
    return;
  }
  if (server.hasArg("threshold"))
    settings.threshold = server.arg("threshold").toFloat();
  if (server.hasArg("refractoryMs"))
    settings.refractoryMs = (uint32_t)server.arg("refractoryMs").toInt();
  if (server.hasArg("toneEnabled"))
    settings.toneEnabled = (uint8_t)server.arg("toneEnabled").toInt();
  if (server.hasArg("toneRatio"))
    settings.toneRatio = server.arg("toneRatio").toFloat();
  if (server.hasArg("tz")) {
    settings.tz = server.arg("tz");
    startNTP();
  } else if (server.hasArg("tzOffset")) {          // legacy fixed offset
    settings.tzOffsetHours = server.arg("tzOffset").toFloat();
    settings.tz = tzFromOffset(settings.tzOffsetHours);
    startNTP();
  }
  bool ok = saveSettings();
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleLog() {
  // Emit real local clock time to the millisecond, plus the raw epoch ms.
  // Rows written before v2.10.0 stored SECONDS; detect by magnitude.
  if (!storageReady()) { server.send(503, "text/plain", "no fs"); return; }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "date,time,epoch_ms,sec_from_hour,peak\n");
  const char* parts[2] = { CHIME_LOG_OLD_PATH, CHIME_LOG_PATH };
  for (int p = 0; p < 2; p++) {
    File f = LittleFS.open(parts[p], "r");
    if (!f) continue;
    while (f.available()) {
      String ln = f.readStringUntil('\n');
      if (ln.length() < 3) continue;
      int comma = ln.indexOf(',');
      if (comma < 1) continue;
      // Parse WITHOUT %llu — the ESP8266's reduced newlib does not support
      // 64-bit conversions in printf/scanf, so the timestamp is split as a
      // decimal string: all but the last 3 digits are seconds, last 3 are ms.
      // Legacy rows (<= 10 digits) hold plain seconds and get ms = 0.
      String tok = ln.substring(0, comma);
      float pk = ln.substring(comma + 1).toFloat();
      unsigned long secs = 0; unsigned msPart = 0;
      if (tok.length() > 10) {
        secs   = strtoul(tok.substring(0, tok.length() - 3).c_str(), nullptr, 10);
        msPart = (unsigned)strtoul(tok.substring(tok.length() - 3).c_str(), nullptr, 10);
      } else {
        secs = strtoul(tok.c_str(), nullptr, 10);
      }
      if (secs < NTP_MIN_EPOCH) continue;          // unusable timestamp
      char d[12], t[14];
      formatLocalParts((uint64_t)secs * 1000ULL + msPart, d, sizeof(d), t, sizeof(t));
      // Signed seconds from the NEAREST top of the hour (+ = after the hour,
      // - = before). This is the number that matters for a striking clock:
      // it is how early or late the strike landed.
      long nearestHour = (long)(((secs + 1800UL) / 3600UL) * 3600UL);
      double fromHour = (double)((long)secs - nearestHour) + (double)msPart / 1000.0;
      char row[120];
      int n = snprintf(row, sizeof(row), "%s,%s,%lu%03u,%+.3f,%.4f\n",
                       d, t, secs, msPart, fromHour, (double)pk);
      server.sendContent(row, n);
      yield();
      // A big log takes seconds to stream and loop() isn't running, so keep
      // the detector listening (chimes are queued, not logged, until done).
      soundSampleOnly();
    }
    f.close();
  }
  server.sendContent("");
}

static void handleLearnStart() {
  analysisLearnArm();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleLearnStatus() {
  LearnResult r = analysisGetLearn();
  char b[160];
  snprintf(b, sizeof(b),
    "{\"armed\":%d,\"valid\":%d,\"f1\":%.1f,\"f2\":%.1f,\"sr\":%.0f,"
    "\"enabled\":%d,\"ratio\":%.3f}",
    analysisLearnArmed() ? 1 : 0, r.valid ? 1 : 0, r.f1, r.f2, r.sr,
    settings.toneEnabled, (double)settings.toneRatio);
  server.send(200, "application/json", b);
}

static void handleGain() {
  SoundState s = soundGetState();
  // Before the first 500ms window closes, rawMin/rawMax still hold their
  // sentinel values (1023/0), which would report a nonsense negative swing.
  int swing = (s.rawMax >= s.rawMin) ? (int)s.rawMax - (int)s.rawMin : 0;
  // Verdict logic for setting the LM393 pot. The raw signal should swing
  // healthily without slamming the rails. Thresholds in ADC counts (0..1023).
  const char* verdict; const char* advice;
  if (s.clipCount > 5) {
    verdict = "clipping"; advice = "Turn the pot DOWN (counter-clockwise) until clipping stops.";
  } else if (swing < 20) {
    verdict = "too low"; advice = "Make some noise near the mic. If still flat, turn the pot UP (clockwise).";
  } else if (swing < 80) {
    verdict = "low"; advice = "Signal is weak. Turn the pot UP a little for more headroom.";
  } else if (swing > 700) {
    verdict = "hot"; advice = "Strong signal. Ease the pot DOWN slightly to avoid clipping on loud chimes.";
  } else {
    verdict = "good"; advice = "Signal sits nicely between the floor and the rails. Leave it here.";
  }
  char buf[360];
  snprintf(buf, sizeof(buf),
    "{\"rawMin\":%u,\"rawMax\":%u,\"swing\":%d,\"clip\":%u,"
    "\"verdict\":\"%s\",\"advice\":\"%s\",\"sps\":%u,\"gapMs\":%.1f}",
    s.rawMin, s.rawMax, swing, s.clipCount, verdict, advice,
    s.sampleRate, (double)s.maxGapUs / 1000.0);
  server.send(200, "application/json", buf);
}

static void handleHistory() {
  // Streams the coarse 1-second history so long plot windows are populated
  // immediately, even right after a browser refresh. Values are integers
  // (excess * HIST_SCALE); the client divides.
  // Streamed straight out of the ring — copying it into a second
  // HIST_SECONDS buffer here cost 7.2 KB of DRAM for no benefit and risked
  // overflowing dram0_0_seg at link time.
  // ?n=<count> returns only the newest n entries: the live plot's gap
  // backfill needs a few hundred at most, and the full hour is ~18 KB.
  uint16_t avail = soundHistoryCount();
  uint16_t n     = avail;
  if (server.hasArg("n")) {
    long want = server.arg("n").toInt();
    if (want >= 0 && want < (long)avail) n = (uint16_t)want;
  }
  uint32_t seq  = soundHistorySeq();
  // Scope bin where the first returned entry starts (entries are HIST_BINS
  // bins each), so the page can line entries up with its 25ms trace.
  uint32_t firstBin = soundHistoryEndBin() - (uint32_t)n * HIST_BINS;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  char head[80];
  int hn = snprintf(head, sizeof(head), "%u,%lu,%u,%lu,%u\n",
                    (unsigned)HIST_SCALE, (unsigned long)seq, n,
                    (unsigned long)firstBin, (unsigned)HIST_BINS);
  server.sendContent(head, hn);   // scale, total seconds, count, firstBin, binsPerEntry

  // Entries are read by absolute number: sampling continues during the
  // stream (soundSampleOnly), and new entries must not shift what we send.
  uint32_t firstSeq = seq - n;
  char chunk[256]; int used = 0;
  for (uint16_t i = 0; i < n; i++) {
    int w = snprintf(chunk + used, sizeof(chunk) - used,
                     "%u%s", soundHistoryAtSeq(firstSeq + i), (i + 1 < n) ? "," : "");
    if (w < 0) break;
    used += w;
    if (used > (int)sizeof(chunk) - 12) {          // flush before overflow
      server.sendContent(chunk, used); used = 0; yield();
    }
    soundSampleOnly();                             // keep the detector listening
  }
  if (used > 0) server.sendContent(chunk, used);
  server.sendContent("");
}

static void handleLogStat() {
  // Answers "why is my chime log empty?" without a serial cable. The three
  // real causes are: filesystem not mounted (Flash Size set to FS:none),
  // NTP not yet synced (strikes can't be timestamped so they aren't logged),
  // or simply nothing detected yet.
  size_t chimeBytes = 0;
  if (storageReady()) {
    File f = LittleFS.open(CHIME_LOG_PATH, "r");
    if (f) { chimeBytes = f.size(); f.close(); }
  }
  SoundState s = soundGetState();
  const char* why = "ok";
  if (!storageReady())            why = "filesystem not mounted (check Flash Size = 4MB FS:2MB)";
  else if (!timeIsValid())        why = "waiting for NTP - strikes are not logged until the clock has real time";
  else if (chimeBytes == 0 && s.chimeCount == 0)
                                  why = "no chimes detected yet - check threshold and mic gain";
  else if (chimeBytes == 0)       why = "chimes detected since boot but none written - they occurred before NTP synced";
  char buf[300];
  snprintf(buf, sizeof(buf),
    "{\"fs\":%d,\"timeValid\":%d,\"chimeBytes\":%u,"
    "\"chimesSinceBoot\":%lu,\"why\":\"%s\"}",
    storageReady() ? 1 : 0, timeIsValid() ? 1 : 0,
    (unsigned)chimeBytes, (unsigned long)s.chimeCount, why);
  server.send(200, "application/json", buf);
}

static void handleReset() {
  // Clears saved WiFi creds -> next boot re-enters provisioning.
  settings.wifiSsid = "";
  settings.wifiPass = "";
  saveSettings();
  server.send(200, "application/json", "{\"ok\":true}");
  g_rebootAt = millis() + 700;            // reboot from loop(), post-flush
}

static void handleNotFound() {
  if (apMode) {
    // Captive-portal-style: bounce unknown hosts to the splash.
    server.sendHeader("Location", String("http://") + AP_IP.toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

// =====================================================================
//  MODE STARTUP
// =====================================================================
static void startAPMode() {
  apMode = true;
  // AP_STA, not AP: the STA interface must be up for WiFi.scanNetworks()
  // to work — with AP-only mode the splash page would list no networks.
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();          // idle STA: stops join retries from a failed
                              // network channel-hopping the SoftAP around
                              // (scans still work on the idle interface)
  rescanNetworks();                // blocking scan BEFORE the AP is up:
                                   // no clients connected yet to disrupt

  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);   // AP_PASS may be "" for open network
  delay(300);

  dnsServer.start(53, "*", AP_IP);  // redirect all DNS to us (captive feel)

  server.on("/",        handleRoot);
  server.on("/scan",    handleScan);
  server.on("/rescan",  handleRescan);
  server.on("/save",    HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  displaySetupScreen(AP_SSID, AP_IP.toString());
}

// Fallback AP mode is also where the device lands after a power cut that
// took the router down too: the ESP8266 boots in seconds, the router takes
// minutes, and the boot-time join times out. While nobody is on the setup
// page, keep retrying the saved network and reboot into normal mode once it
// joins. A join attempt moves the radio to the router's channel, which would
// drop a phone on the setup AP, so it only runs with zero AP clients and is
// abandoned the moment one connects.
static void apRetrySavedNetwork() {
  static uint32_t lastTry = 0, tryStart = 0;
  static bool trying = false;
  if (settings.wifiSsid.length() == 0 || g_rebootAt) return;
  uint8_t clients = WiFi.softAPgetStationNum();
  if (trying) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("[AP] saved network is back -> rebooting into normal mode"));
      g_rebootAt = millis() + 500;
    } else if (clients > 0 || millis() - tryStart > AP_RETRY_JOIN_MS) {
      WiFi.disconnect();   // stop join retries hopping the AP's channel
      trying  = false;
      lastTry = millis();
    }
    return;
  }
  if (clients == 0 && millis() - lastTry > AP_RETRY_EVERY_MS) {
    Serial.println(F("[AP] retrying saved network"));
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    trying   = true;
    tryStart = millis();
  }
}

static bool startSTAMode() {
  apMode = false;
  WiFi.mode(WIFI_STA);
#ifdef USE_STATIC_IP
  WiFi.config(STATIC_IP, STATIC_GATEWAY, STATIC_SUBNET, STATIC_DNS);
#endif
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());

  displayConnecting(settings.wifiSsid);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    yield();
  }
  if (WiFi.status() != WL_CONNECTED) return false;

  startNTP();

  server.on("/",        handleRoot);
  server.on("/api/state",  handleState);
  server.on("/api/config", HTTP_GET,  handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigSet);
  server.on("/api/log",    handleLog);
  server.on("/api/learn/start",  HTTP_POST, handleLearnStart);
  server.on("/api/learn/status", handleLearnStatus);
  server.on("/api/gain",         handleGain);
  server.on("/api/logstat",      handleLogStat);
  server.on("/api/history",      handleHistory);
  server.on("/api/reset",  handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  // Browser OTA at /update — register before begin() so the routes exist.
#ifdef OTA_PASSWORD
  httpUpdater.setup(&server, "/update", "admin", OTA_PASSWORD);
#else
  httpUpdater.setup(&server, "/update");
#endif

  // mDNS: dashboard reachable at http://MDNS_HOSTNAME.local
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);

  // OTA: wireless reflash (device may end up sealed inside the clock case)
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.begin();

  Serial.print(F("[OTA] browser update page: http://"));
  Serial.print(WiFi.localIP());
  Serial.println(F("/update"));

  displayConnected(WiFi.localIP().toString());
  return true;
}

// =====================================================================
//  CHIME CALLBACK  — fired by sound module on a confirmed chime
// =====================================================================
static void onChime(float peak, uint32_t onsetAgeMs) {
  // The callback arrives CHIME_PEAK_WINDOW_MS after the strike began; wind
  // the timestamp back so the log records the true onset instant.
  unsigned long long now = epochMillis();
  unsigned long long ms  = (now > onsetAgeMs) ? (now - onsetAgeMs) : now;
  unsigned long epoch   = (unsigned long)(ms / 1000ULL);
  soundNoteChimeEpoch(epoch);
  if (epoch >= NTP_MIN_EPOCH) logChime(ms, peak);   // full ms precision
  displayChimeFlash(peak);
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("\n[GFC] v" FW_VERSION " booting"));

  WiFi.persistent(false);   // we manage creds in LittleFS; stop the SDK
                            // rewriting them to flash on every connect

  displayBegin();
  displaySplash();

  storageBegin();           // LittleFS; tolerates failure (log disabled)
  loadSettings();           // pulls creds + tuning from config.json
  soundBegin(onChime);      // sets up A0 sampling + envelope detection

  if (settings.wifiSsid.length() == 0) {
    Serial.println(F("[GFC] no creds -> AP provisioning"));
    startAPMode();
  } else if (!startSTAMode()) {
    Serial.println(F("[GFC] STA failed -> AP provisioning"));
    startAPMode();
  }
}

void loop() {
  if (apMode) {
    dnsServer.processNextRequest();
    apRetrySavedNetwork();
    static uint32_t lastApLog = 0;
    if (millis() - lastApLog > 5000) {
      lastApLog = millis();
      Serial.printf("[AP] clients:%d heap:%u\n",
                    WiFi.softAPgetStationNum(), ESP.getFreeHeap());
    }
  } else {
    MDNS.update();
    ArduinoOTA.handle();
  }
  server.handleClient();

  if (g_rebootAt && (int32_t)(millis() - g_rebootAt) >= 0) ESP.restart();

  // Mic sampling only in STA mode: the ESP8266 ADC is shared with the
  // RF subsystem, and 500 Hz analogRead during AP provisioning is a known
  // cause of unstable WiFi / dropped clients. We don't need it there.
  if (!apMode) soundUpdate();

  // Periodic OLED refresh in STA mode (live level + last chime). Each push
  // blocks mic sampling, so keep it slow — see OLED_REFRESH_MS.
  static uint32_t lastUi = 0;
  if (!apMode && millis() - lastUi > OLED_REFRESH_MS) {
    lastUi = millis();
    displayLive(soundGetState(), settings.threshold, timeIsValid());
  }

  yield();
}
