# Grandfather Clock Tool — Project Summary (v2.15.0, current)

> v2.10.0 answers a feedback round. **(1) BUG — ambient floor pollution:**
> the quiet-floor EMA was symmetric (0.0015 both ways), so a loud chime dragged
> the floor up with it and `excess` clamped to zero for ~3.2 s afterwards — the
> live plot went dead ("cooling off period"), which made setting mic sensitivity
> nearly impossible. Now asymmetric: rise 0.00008 (~25 s, transients barely move
> the floor), fall 0.004 (~0.5 s). Simulated blind time 3.22 s -> 0.00 s, while
> the floor still adapts up to a genuinely louder room over ~60 s. **(2)** Chime
> log now stores epoch MILLISECONDS; /api/log and /api/drift emit a
> `datetime_local` column formatted `YYYY-MM-DD HH:MM:SS.mmm` (legacy
> seconds-rows auto-detected by magnitude). drift.csv gained a trailing ms
> column. **(3)** New in-browser **Log viewer** panel: browse drift or chime
> rows in a table, and DELETE a bad drift measurement (/api/drift/delete
> rewrites the CSV and reloads the ring) — one mis-counted event visibly skews
> the regression. **(4)** Scope X axis is user-selectable (10 s / 30 s / 1 / 2 /
> 5 min); the client ring now holds 5 min and draws the chosen span.
> **(5)** New **tick sensitivity tool** (/api/tick/listen + ticksDiagnose()):
> listens 4-15 s and reports onsets, median interval, bph, regularity and a
> plain verdict (silent / faint / good / noisy) with pot advice — aimed at
> ticks, which are 30-40 dB below chimes, unlike the existing gain tool.
> Also fixed a leftover hardcoded blue on `button.primary:hover` from the
> pre-brass theme. Host suite 93 -> 100 checks.

> v2.9.0 replaces the generic dark UI with the **Brass & Walnut** clock theme:
> walnut browns (#191310/#231b15), aged-brass accent (#c9a227), warm ivory ink,
> Cormorant Garamond display type over Inter body / JetBrains Mono numerals.
> Canvas colours are no longer hard-coded — the scope and timegrapher read
> --grid/--trace/--thresh/--axis via a CV() helper, so graphs follow the theme
> (brass trace on a warm well, not phosphor green). Retheming in future = edit
> the :root block only. The AP setup splash was rethemed to match and remains
> fully self-contained (verified 0 external refs — captive clients have no
> internet; it uses Georgia/system stacks instead of web fonts). Layout,
> element IDs, JS and the API surface are unchanged; presentation only.
> Host suite 91/91.

> v2.8.1 is a review/bug-fix release. **(1) Stopped-clock alarm 32-bit
> overflow:** the check multiplied seconds by 1000 and compared against
> STOP_ALARM_MS; `unsigned long` is 32-bit on the ESP8266, so at ~49.7 days
> of silence the product wrapped and the alarm silently CLEARED. Now compares
> in seconds (no multiply). NOTE: the host suite could NOT catch this because
> the host's `unsigned long` is 64-bit — a permanent blind spot; integer-width
> assumptions must be reasoned about, and a test now verifies the arithmetic
> form explicitly in uint32_t. **(2) Tick capture vs strike collision:**
> ticksCapture() blocks ~12s, during which soundUpdate() does not run and
> strikes are NOT heard; a capture overlapping a strike train dropped strikes
> and invalidated that hour's drift measurement. Captures now defer while a
> strike event is open (new horoEventOpen()) and within TICK_STRIKE_GUARD_S
> (90s) of :00 (and :30 when half-hour mode is on). Hour accuracy outranks
> tick sampling. **(3)** /api/gain reported a negative swing before the first
> 500ms telemetry window; now clamped to 0. **(4)** removed a pointless
> close() on a failed file open. Host suite 74 -> 91 checks.

> v2.8.0 adds two usability tools. **Help mode**: a "? Help" toggle in the
> header; when on, elements with a data-help attribute reveal a titled tooltip
> on hover (desktop) or tap (mobile). Driven by one data-help string per
> element ("tt:Title|Body"), easy to extend. **Mic gain calibration**: a new
> panel + /api/gain endpoint. sound.cpp now tracks raw ADC min/max swing and
> near-rail clip count over a 500ms window (added to SoundState); the endpoint
> returns a plain-language verdict (too low / low / good / hot / clipping) and
> advice for turning the LM393 pot. Pure additions; host suite still 74/74.
> Also folds in v2.7.2's wrap-safe tick sampling fix.

> v2.7.1 redesigns the web UI: modern dark theme (slate/charcoal, blue accent,
> Inter/Space Grotesk/JetBrains Mono via web fonts on the STA dashboard only),
> full-width responsive two-column layout that reflows to a clean single-column
> mobile page, restructured panels (Live signal / Timegrapher / Horology /
> Detection tuning / Logs). The AP setup splash was also restyled to match but
> stays fully self-contained (NO external fonts — captive clients have no
> internet). All element IDs and the entire JS/API surface are unchanged; this
> is a pure presentation patch. Phosphor-CRT theme retired.

> v2.7.0 adds **escapement tick analysis (timegrapher)** [Phase E], behind
> settings.tickEnabled (default OFF — the risky feature is opt-in so it can't
> destabilize the proven core). New file ticks.h/.cpp. Windowed A0 capture at
> 2kHz for 12s, streaming onset detection (stores only timestamps), then pure
> analyzeBeats() math: rate (s/day, from measured vs nominal half-beat) and
> beat error (ms, alternating-interval asymmetry). Parity classification with
> resync-on-glitch + median gate (0.78–1.28x) + capture-quality reject.
> Calibration sets current beat as nominal target. Dashboard timegrapher:
> two-dot-line plot (slope=rate, gap=beat error). New API: /api/tick,
> /api/tick/arm, /api/tick/calibrate, /api/tick/log; /api/config takes
> tickEnabled. Persistence /tick.csv (rotates 128KB). Host suite 74/74 — the
> beat math was developed test-first and caught a rate-bias bug (pooled vs
> symmetric mean) and a glitch-desync bug during development.
> KNOWN UNVERIFIED: whether the LM393 actually hears the escapement at real
> mic distance, and whether 12s captures disturb WiFi — both on-hardware only.

> v2.6.0 adds: BME280 environmental sensing (Adafruit lib, shared I2C bus at
> SDA=GPIO14/SCL=GPIO12, addr 0x76/0x77, temperature stamped into each drift
> measurement); half-hour strike support (config toggle, doubles measurement
> rate); clock-stopped alarm (75-min silence -> OLED banner + dashboard alert,
> device/dashboard only, no push); wind reminder + health logging. New files:
> environment.h/.cpp (17 total). New API: /api/env, /api/wind (POST);
> /api/horology gains stopped/wind/temp fields; /api/config accepts halfHour +
> windDays. Host suite now 60/60. Library added: Adafruit_BME280 + Adafruit_Sensor.


> This document supersedes the Rev012 summary entirely. That doc describes the
> SD-card era and pre-fix pin mappings — several of its "facts" are now known
> to be wrong (see Hardware). Remove it from project instructions.

## What this is
An ESP8266-based chime monitor and horological instrument for a grandfather
clock: detects chimes acoustically, identifies them by learned frequency
signature, logs hour-strike timing offsets against NTP, computes pendulum
drift rate, and recommends rating-nut adjustments based on a learned
sensitivity model. Web dashboard + OLED, provisioned over a SoftAP splash.

## Versioning (semver: MAJOR=breaking hw/storage, MINOR=features, PATCH=fixes)
- 0.1.0 Rev010 MPU6050 (abandoned) → 0.2.0 Rev011 LM393/D1 Mini
- 1.0.0 Rev012: FORIOT board + SD card + custom SoftAP splash + OLED
- 1.0.1 five review fixes (AP_STA scan, real rescan, deferred reboot, time.h, 400kHz I²C)
- 2.0.0 **SD card removed** → LittleFS on onboard flash (breaking: wiring + storage)
- 2.1.0 mDNS + ArduinoOTA; self-contained AP splash; AP-fallback stability; snprintf state JSON
- 2.1.1 **OLED pins corrected** (vendor listing was reversed) + GPIO16 reset pulse
- 2.1.2 AP provisioning stability: no ADC sampling in AP mode, scan-before-AP, async rescan, serial telemetry
- 2.2.0 binned scope streaming (25ms peak bins + sequence numbers)
- 2.3.0 auto/adjustable Y scale, 30s window, 800ms bin coverage per poll
- 2.4.0 log (60 dB) scale, time-interpolated smooth scrolling, hi-DPI canvas
- 2.5.0 **scientific instrument features**: FFT chime learning, Goertzel tone gate, hour-strike offset measurement, drift regression, pendulum adjustment advisor
- 2.5.1 f2 acceptance-gate fix (power vs amplitude domain) — caught by the new host test suite

`FW_VERSION` in `config.h`; reported on serial boot banner, OLED splash, and the `fw` field of `/api/state` (confirms OTA flashes took).

## Hardware (current, total wiring = 3 wires)
- **Board:** FORIOT ESP8266 NodeMCU w/ built-in 0.96" SSD1306 OLED.
  **OLED I²C: SDA=GPIO14 (D5), SCL=GPIO12 (D6), addr 0x3C — empirically verified
  by I2C scan; the Amazon listing has these REVERSED.** `displayBegin()` also
  pulses GPIO16 low→high (Heltec-style OLED reset; harmless if unwired).
- **Mic:** LM393 analog module. AO→A0, VCC→3V3 (not 5V), GND→GND. This board's
  A0 accepted the 3V3-powered module directly (working in practice); the
  10k/22k divider fallback was never needed.
- **No SD card.** Storage is LittleFS on onboard flash. Pins D0–D4, D7, D8 free.

## IDE / build
- Board: **NodeMCU 1.0 (ESP-12E Module)**, ESP8266 community core
- Flash Size: **must include FS space**, e.g. "4MB (FS:2MB)" — FS:none silently
  disables settings + all logs (serial warns)
- Libraries: **Adafruit_SSD1306 + Adafruit_GFX only** (SdFat no longer used;
  mDNS/OTA ship with the core)
- Reflash wirelessly: **ArduinoOTA** — IDE Tools→Port→"grandfatherclock" network
  port. Optional `OTA_PASSWORD` define in config.h (off by default).

## Firmware structure (15 files)
`GrandfatherClock.ino`, `config.h`, `settings.h/.cpp`, `storage.h/.cpp`,
`sound.h/.cpp`, `display.h/.cpp`, `webpages.h`, `analysis.h/.cpp`, `horology.h/.cpp`

- **sound**: 500 Hz A0 sampling (STA mode only — ADC suppressed in AP mode,
  it destabilizes WiFi); self-zeroing DC center; fast EMA envelope (a 2.16.0
  peak-hold envelope was reverted in 2.16.2 after chimes stopped registering
  on hardware) vs slow asymmetric ambient EMA; excess = envelope−ambient; threshold +
  refractory trigger. Samples/s and longest unsampled gap are reported on
  `/api/gain`; OLED refreshes at 2 Hz because each push blocks sampling. Two-stage
  detection: envelope proposes, Goertzel tone gate confirms (if enabled).
  Also folds excess into 25ms peak bins (ring of 256, ~6.4s) for scope streaming.
  Every 40 bins (exactly 1 s; fixed bin cadence) make one 1-hour-history entry, so
  the page backfills live-trace gaps (throttled background tab) from `/api/history?n=`,
  whose header is `scale,seq,count,firstBin,binsPerEntry`.
- **analysis**: DSP. Learning = armed capture of 1024 samples at ~8kHz (paced
  analogRead burst; TRUE sample rate measured with micros and used in all
  frequency math) → Hann → radix-2 FFT → top peak w/ parabolic interpolation
  + secondary peak (power ratio ≥0.03). Detection = 256-sample burst →
  Goertzel at learned f1/f2 (each evaluated at ±1 bin for detuning tolerance)
  → pass if tone energy fraction ≥ `toneRatio` (default 0.20).
- **horology**: strikes group into events (6s gap closes); count vs expected
  local hour (13:00→1, 00:00→12); offset = first strike vs nearest top-of-hour
  (ms precision, + = late); valid requires count match AND |offset|<600s
  (auto-rejects half-hour single strikes and miscounts). Drift rate = least
  squares of offset vs time over valid points since the last adjustment
  (needs ≥2 pts spanning ≥2h), in s/day. Adjustments log (epoch, turns,
  rateBefore snapshot); sensitivity k (s/day per turn) averaged across
  adjustment boundaries incl. the live window; recommendation = −rate/k.
  Sign convention: **+ turns = raising bob = speeding up** (user-consistent).
  Logs: `/drift.csv`, `/adjust.csv` — reloaded into RAM rings at boot.
- **storage**: LittleFS; chime log `/chimes.csv` rotates at 128KB to
  `/chimes.old.csv`; `/api/log` streams old+current stitched.
- **settings**: hand-rolled JSON at `/config.json` (no ArduinoJson): wifi
  creds, threshold, refractoryMs, tz (POSIX rule incl. DST; migrated from the
  legacy fixed tzOffset), toneEnabled, toneF1/F2, toneRatio.
- **display**: context screens (setup AP info / connecting / connected-IP /
  live meters / 700ms CHIME banner). Wire at 400kHz (~6ms full-frame).
- **provisioning**: no creds (or STA fail) → SoftAP `GrandfatherClock-Setup`,
  splash at 192.168.4.1, DNS captive redirect. Blocking WiFi scan runs BEFORE
  the AP starts (a scan while clients are attached channel-hops them off);
  rescan button uses async scan. STA iface idled via WiFi.disconnect() in AP
  fallback. `WiFi.persistent(false)`. Reboots deferred via `g_rebootAt` flag
  (never delay/restart inside HTTP handlers). Setup page is self-contained —
  NO external fonts (AP clients have no internet; render-blocking fetch hangs).
- **web dashboard** (STA; phosphor-CRT aesthetic, Orbitron/Share Tech Mono ok
  here): scope trace — 30s window, 25ms bins streamed with sequence numbers
  in `/api/state`, time-interpolated 60fps scrolling, hi-DPI canvas,
  linear/log(60dB) + auto/fixed Y controls, threshold line; stat tiles;
  threshold/refractory sliders; Horology panel (drift rate, last strike +
  offset, learned k, recommended turns, adjustment input, Learn-chime button,
  tone filter toggle); CSV downloads (chimes + drift); WiFi reset.
- **network**: mDNS `http://grandfatherclock.local`; ArduinoOTA.

## HTTP API
`/` dashboard · `/api/state` (level/ambient/peak/threshold/chimes/lastChime/
epoch/fw/scopeFrom/scopeSeq/scope[]; `?since=<bin>` returns only bins the page
hasn't seen, max 64 per response) · `/api/config` GET/POST (threshold, refractoryMs,
tz, toneEnabled, toneRatio; GET also returns device localTime) · `/api/log`, `/api/drift` CSV ·
`/api/learn/start` POST, `/api/learn/status` · `/api/adjust` POST turns ·
`/api/horology` · `/api/reset`. AP mode: `/`, `/scan`, `/rescan`, `/save`.

## Testing
Host-side suite (`GrandfatherClock_hosttests.zip`) compiles the REAL
analysis/horology/settings sources against mock Arduino/LittleFS headers with
g++. 45 checks: FFT peak recovery on synthetic two-tone chime, Goertzel gate
accept/reject matrix, event/offset/hour-mapping math (incl. negative offsets,
half-hour rejection, midnight), regression + k-learning + prediction numbers,
settings JSON round-trip (incl. quoted SSIDs), CSV persistence across
simulated reboot; embedded page JS syntax-checked via node. Claude runs this
suite in-sandbox before each release; suite caught the v2.5.1 bug.
**Still only verifiable on real hardware:** Arduino IDE compile, ADC burst
timing under WiFi, real chime acoustics vs toneRatio.

## Hard-won lessons (do not relearn)
1. **Never trust vendor pinouts** — OLED SDA/SCL were listed reversed. The
   `OLED_Finder.ino` I2C scanner sketch settles it in 2 seconds; keep it.
2. ESP8266 ADC fights WiFi — no fast analogRead during AP mode; bursts only,
   brief, with measured (not assumed) sample rate.
3. Blocking WiFi scans drop connected AP clients (the phone loading the page).
4. Defer reboots/portal ops out of HTTP handlers (`g_rebootAt` in loop()).
5. No external resources on the AP-mode splash — captive clients have no internet.
6. GPIO15/D8 + module CS pull-ups can block boot; GPIO12 is HW-SPI MISO and
   this board's OLED SCL — both moot now that SD is gone, but relevant if
   hardware returns.
7. iPhone + no-internet APs: cellular data hijacks requests to 192.168.4.1.
8. String concat in hot API paths fragments the heap — snprintf fixed buffers.

## Current state / next steps
- v2.5.1 flashed path: provisioning ✓, mDNS ✓, dashboard + scope ✓, tap
  detection ✓. Not yet exercised on real chimes: Learn mode, tone filter,
  hourly offset capture, advisor (needs one adjustment cycle to learn k).
- Suggested next: periodic STA retry from AP fallback (self-heal after
  router outages); possibly a drift/offset history chart on the dashboard.
