#include <Arduino.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <SPI.h>
#include <ScratchWorkspace.h>
#include <builtinFonts/all.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <ctime>
#include <sys/time.h>  // struct timeval / settimeofday() — not part of <ctime>

#include "AppVersion.h"
#include "ApScanCache.h"
#include "BleScanner.h"
#include "CaptureControl.h"
#include "DeauthDetector.h"
#include "DeauthEngine.h"
#include "EncryptedLog.h"
#include "MappedInputManager.h"
#include "PcapWriter.h"
#include "RecentSightings.h"
#include "RubyAppState.h"
#include "RubySettings.h"
#include "Screenshot.h"
#include "SignalCatalog.h"
#include "TargetList.h"
#include "WifiSniffer.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "fontIds.h"
#include "ruby/RubyManager.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts — Space Mono only (see lib/EpdFont/builtinFonts/all.h for why the reading-typography
// families from upstream CrossPlant were dropped, and for the monospace/instrument-readout choice).
EpdFont smallFont(&space_mono_8_regular);
EpdFontFamily smallFontFamily(&smallFont);
EpdFont ui10RegularFont(&space_mono_10_regular);
EpdFont ui10BoldFont(&space_mono_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);
EpdFont ui12RegularFont(&space_mono_12_regular);
EpdFont ui12BoldFont(&space_mono_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

namespace {

// RTC_NOINIT survives ESP.restart() (but not power loss) — used for the periodic heap-defrag
// reboot to skip the splash and land straight back on the dashboard.
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC81D7107;
// A long-running capture session fragments the heap (ArduinoJson allocations on every settings/
// catalog save, BLE/WiFi observation churn). Rebooting periodically is cheap insurance against a
// multi-day-uptime OOM; the encrypted log and all persisted counters survive since they're on SD.
constexpr unsigned long HEAP_DEFRAG_INTERVAL_MS = 12UL * 60 * 60 * 1000;  // 12 hours
// Raw handshake capture (see WifiSniffer/PcapWriter) does its own SD open/write/close cycles on
// top of the encrypted log's, on a device that already runs with a razor-thin DMA-pool margin
// (see HEAP_DMA_*_THRESHOLD below) — real overnight use fragmented that pool enough to abort in
// ~1.5 hours instead of the 12-hour window this interval was originally sized for. Until that's
// fixed at the source, fall back to a much shorter interval whenever raw capture is on, so a
// silent reboot always happens well before the fragmentation has a chance to catch up.
constexpr unsigned long HEAP_DEFRAG_INTERVAL_MS_RAW_CAPTURE = 1UL * 60 * 60 * 1000;  // 1 hour
// Checked every loop() — if the DMA-capable pool (what the hardware AES engine needs for
// EncryptedLog's GCM calls) gets fragmented below this, proactively silent-restart rather than
// wait for the next GCM call to fail with an uncontrolled abort(). Picked well above the ~4 bytes
// observed at the point of an actual failure, so there's real margin to notice and react.
constexpr size_t HEAP_DMA_LARGEST_BLOCK_MIN = 4096;
// Deliberately longer than a simple debounce: the device rides around in a bag/pocket while
// capturing, and a short jostle against the power button should not wake (and start refreshing
// the display / burning battery) every time it gets bumped.
constexpr unsigned long POWER_BUTTON_WAKE_DEBOUNCE_MS = 500;
constexpr unsigned long POWER_LONG_PRESS_MS = 1200;

}  // namespace

RTC_NOINIT_ATTR uint32_t silentRebootMagic;
// Counts consecutive silent restarts (see silentRestart() below) — survives ESP.restart() the
// same way silentRebootMagic does. Real hardware testing found a case where BLE passive scan
// starting at boot fragmented the DMA-capable pool down below HEAP_DMA_LARGEST_BLOCK_MIN
// immediately, every single boot — the circuit breaker below correctly detected that and
// restarted, but since the exact same fragmentation reproduced identically on the very next boot,
// the device was stuck silently restarting every ~1 second forever, which looks and feels exactly
// like a hang from the outside. This counter is how setup() notices "restarting alone isn't fixing
// this" and forces the likely-guilty settings off instead of trying the same thing again.
RTC_NOINIT_ATTR uint32_t silentRebootCount;
// Loops of 3 consecutive silent restarts within the same boot chain are treated as unrecoverable
// by restarting alone — see the crash-loop guard in setup().
constexpr uint32_t kMaxConsecutiveSilentReboots = 3;
static unsigned long allowSleepAt = 0;
static unsigned long bootMillis = 0;

void silentRestart() {
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  silentRebootCount++;
  LOG_DBG("MAIN", "Silent restart (heap defrag), consecutive count now %u",
         static_cast<unsigned>(silentRebootCount));
  delay(50);
  ESP.restart();
}

LogRecordType mapWifiKindToLogType(WifiFrameKind kind) {
  switch (kind) {
    case WifiFrameKind::Beacon:
    case WifiFrameKind::ProbeResponse:
      return LogRecordType::WifiAp;
    case WifiFrameKind::ProbeRequest:
      return LogRecordType::WifiClient;
    case WifiFrameKind::EapolHandshake:
      return LogRecordType::WifiHandshake;
    case WifiFrameKind::Other:
    default:
      return LogRecordType::WifiAp;
  }
}

void startCaptureIfEnabled() {
  if (SETTINGS.wifiSniffEnabled) {
    const uint8_t* channels;
    size_t channelCount;
    WifiSniffer::channelPlanFor(SETTINGS.wifiChannelScope, channels, channelCount);
    wifiSniffer.start(channels, channelCount, SETTINGS.wifiChannelDwellMs, SETTINGS.rawHandshakeCaptureEnabled);
  }
  if (SETTINGS.bleSniffEnabled && SETTINGS.rawHandshakeCaptureEnabled) {
    // Both draw heavily from the same DMA-capable pool at once — real-hardware testing showed
    // this combination reliably fragments it enough to crash-loop (see CHANGELOG and the
    // crash-loop guard above). SettingsActivity now refuses to create this combination going
    // forward, but a settings.json already persisted with both on (from before that fix, or
    // hand-edited) would still hit it at boot without this — prefer raw capture, since that's the
    // core recon workflow BLE is only a passive nice-to-have next to.
    LOG_ERR("MAIN", "BLE passive scan + raw handshake capture both enabled — disabling BLE to avoid a crash-loop");
    SETTINGS.bleSniffEnabled = false;
    SETTINGS.saveToFile();
  }
  if (SETTINGS.bleSniffEnabled) {
    bleScanner.start();
  }
  // Forcing a handshake via deauth is pointless disruption if raw capture isn't on to catch it —
  // refuse to run active mode without it, regardless of what the setting says.
  deauthEngine.setEnabled(SETTINGS.activeDeauthEnabled && SETTINGS.rawHandshakeCaptureEnabled);
}

void enterDeepSleep() {
  HalPowerManager::Lock powerLock;

  wifiSniffer.stop();
  bleScanner.stop();
  deauthEngine.setEnabled(false);

  APP_STATE.totalCaptureSeconds += (millis() - bootMillis) / 1000;
  APP_STATE.saveToFile();
  SIGNAL_CATALOG.saveToFile();
  RUBY.tick();

  activityManager.goToSleep(false);
  delay(400);  // let the sleep screen's refresh physically finish before cutting power to radios/CPU

  if (halTiltSensor.isAvailable()) {
    halTiltSensor.deepSleep();
  }
  display.deepSleep();
  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts() {
  display.begin(false);
  renderer.begin();
  if (!ScratchWorkspace::initialize()) {
    LOG_ERR("MAIN", "Scratch workspace init failed");
  }
  activityManager.begin();

  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);

  renderer.insertFont(FONT_SMALL_ID, smallFontFamily);
  renderer.insertFont(FONT_UI_10_ID, ui10FontFamily);
  renderer.insertFont(FONT_UI_12_ID, ui12FontFamily);

  // Landscape, native panel orientation (792x528 logical — LandscapeCounterClockwise is the
  // identity transform, no rotation math needed). Button *behavior* is unaffected: MappedInputManager
  // is a direct passthrough to hardware, unaware of screen orientation, so Back/Confirm/Left/Right/
  // Up/Down still do exactly what they did in portrait. What changes is purely visual: Chrome's
  // footer becomes a vertical strip of rotated-text hint pills along the screen's right edge
  // instead of a horizontal bar along the bottom — see Chrome::drawFooterHints — because that's
  // the edge the physical button row lands on under this orientation (the same edge portrait's
  // bottom-aligned footer sat on, per Portrait's own coordinate rotation).
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);

  LOG_DBG("MAIN", "Display and fonts ready");
}

void setup() {
  bootMillis = millis();

#ifdef ENABLE_SERIAL_LOG
  delay(250);
  logSerial.setRxBufferSize(1024);
  logSerial.setTxBufferSize(1024);
  Serial.begin(115200);
  logSerial.setTxTimeoutMs(1);
#endif

  HalSystem::begin();

  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  silentRebootMagic = 0;
  if (!isSilentReboot) {
    // A real power-on/wake, not a link in a silentRestart() chain — start the consecutive-count
    // fresh rather than trust whatever RTC_NOINIT_ATTR happened to retain (undefined on true
    // power-on; stale but harmless on a deep-sleep wake, which also isn't a silent reboot).
    silentRebootCount = 0;
  }

  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    activityManager.goToFullScreenMessage("SD card error — capture cannot start", EpdFontFamily::BOLD);
    return;
  }

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    // Not a real wake — just the periodic timer redrawing the sleep screen to fight e-ink
    // ghosting while parked (see HalPowerManager::startDeepSleep's SLEEP_SCREEN_REFRESH_INTERVAL_US;
    // only fires on USB power, the battery latch circuit cuts RTC power too on battery). Redraw
    // and go straight back to sleep — no capture restart, no dashboard, no bootCount/panic
    // bookkeeping for what isn't really a boot.
    SETTINGS.loadFromFile();
    SIGNAL_CATALOG.loadFromFile();
    setupDisplayAndFonts();
    activityManager.goToSleep(false);
    delay(400);  // let the refresh physically finish before cutting power again
    if (halTiltSensor.isAvailable()) {
      halTiltSensor.deepSleep();
    }
    display.deepSleep();
    LOG_DBG("MAIN", "Periodic sleep-screen refresh done, re-entering deep sleep");
    powerManager.startDeepSleep(gpio);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  APP_STATE.bootCount++;
  APP_STATE.saveToFile();

  // Crash-loop guard: restarting alone hasn't broken whatever's causing repeated silent restarts
  // (most plausibly BLE passive scan's controller/host buffers fragmenting the same DMA-capable
  // pool EncryptedLog's hardware AES needs — see silentRebootCount's comment above — reproducing
  // identically on every boot). Force the DMA-hungry opt-ins off and fall through to a normal,
  // visible boot screen instead of the silent-restart splash-skip, so recovery is visible rather
  // than looking like the device is still just hanging.
  bool skipBootSplash = isSilentReboot;
  if (isSilentReboot && silentRebootCount >= kMaxConsecutiveSilentReboots) {
    LOG_ERR("MAIN", "%u consecutive silent restarts — disabling BLE/raw capture/active deauth to break the loop",
           static_cast<unsigned>(silentRebootCount));
    SETTINGS.bleSniffEnabled = false;
    SETTINGS.rawHandshakeCaptureEnabled = false;
    SETTINGS.activeDeauthEnabled = false;
    SETTINGS.saveToFile();
    silentRebootCount = 0;
    skipBootSplash = false;
  }

  // Bridge the X3's battery-backed DS3231 RTC into POSIX time so log timestamps are real
  // calendar time out of the box. X4 has no RTC chip — its clock stays unset (and log records
  // fall back to boot-relative timestamps, see EncryptedLog) unless a future settings screen
  // sets it manually.
  if (halClock.isAvailable()) {
    uint16_t year;
    uint8_t month, day, hour, minute;
    if (halClock.getDateTime(year, month, day, hour, minute)) {
      struct tm tmVal = {};
      tmVal.tm_year = year - 1900;
      tmVal.tm_mon = month - 1;
      tmVal.tm_mday = day;
      tmVal.tm_hour = hour;
      tmVal.tm_min = minute;
      time_t t = mktime(&tmVal);
      struct timeval tv = {t, 0};
      settimeofday(&tv, nullptr);
    }
  }

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    gpio.verifyPowerButtonWakeup(POWER_BUTTON_WAKE_DEBOUNCE_MS, /*shortPressAllowed=*/true);
  }

  LOG_DBG("MAIN", "Starting Ruby " RUBY_VERSION);

  setupDisplayAndFonts();

  if (!skipBootSplash) {
    activityManager.goToBoot();
  }

  SIGNAL_CATALOG.loadFromFile();
  RUBY.begin();
  if (!encryptedLog.begin()) {
    LOG_ERR("MAIN", "Encrypted log failed to initialize — captures will not be persisted to disk");
  }
  if (!pcapWriter.begin()) {
    LOG_ERR("MAIN", "Pcap writer failed to initialize — raw handshake capture will not be persisted to disk");
  }
  targetList.begin();
  deauthEngine.begin();

  wifiSniffer.setObservationCallback([](const WifiObservation& obs) {
    if (obs.kind == WifiFrameKind::Deauth || obs.kind == WifiFrameKind::Disassoc) {
      // Not a device sighting — someone else's deauth/disassoc activity in the air (a
      // half-duplex radio can't hear its own transmission, so this is never DeauthEngine's own
      // burst — see DeauthDetector.h). Feeds DeauthDetector only: the encrypted log/Recent
      // Devices/SignalCatalog paths don't have a meaningful record type or dedup bucket for "this
      // BSSID was targeted by a deauth frame" and would otherwise show it mislabeled as an
      // ordinary AP sighting via mapWifiKindToLogType()'s fallback.
      deauthDetector.onObservation(obs);
      return;
    }
    const LogRecordType type = mapWifiKindToLogType(obs.kind);
    encryptedLog.appendWifi(obs, type);
    SIGNAL_CATALOG.observeWifi(obs);
    recentSightings.recordWifi(obs, type);
    deauthEngine.onObservation(obs);
    apScanCache.observe(obs);
  });
  wifiSniffer.setRawFrameCallback(
      [](const RawFrameCapture* frames, size_t count) { pcapWriter.writeFrames(frames, count); });
  bleScanner.setObservationCallback([](const BleObservation& obs) {
    encryptedLog.appendBle(obs);
    SIGNAL_CATALOG.observeBle(obs);
    recentSightings.recordBle(obs);
  });
  SIGNAL_CATALOG.setNewUniqueCallback(
      [](RfEventType type, const MacAddress& mac) { RUBY.onSignalEvent(type, mac); });

  startCaptureIfEnabled();

  if (HalSystem::isRebootFromPanic()) {
    activityManager.goToCrashReport();
  } else {
    activityManager.goHome();
  }

  // Ensure we're not still holding the power button before leaving setup (avoids the release
  // edge firing a sleep/refresh action the instant loop() starts).
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
  allowSleepAt = millis() + 1000;
}

void loop() {
  static unsigned long lastMemPrint = 0;

  gpio.update();
  wifiSniffer.tick();
  bleScanner.tick();
  SIGNAL_CATALOG.tick();
  RUBY.tick();
  encryptedLog.tick();
  pcapWriter.tick();

  if (Serial && millis() - lastMemPrint >= 15000) {
    LOG_INF("MEM", "Free: %d bytes, MinFree: %d bytes | WiFi frames dropped: %lu", ESP.getFreeHeap(),
            ESP.getMinFreeHeap(), static_cast<unsigned long>(wifiSniffer.framesDropped()));
    lastMemPrint = millis();
  }

  // Circuit breaker: EncryptedLog's hardware-accelerated AES-GCM needs a contiguous chunk of the
  // DMA-capable pool for every write, and that pool is small enough on this chip that a
  // long-running capture session can fragment it down to nothing — which previously showed up as
  // an uncontrolled abort() (see CHANGELOG). Restarting here instead is the same silent, seamless
  // reboot the heap-defrag timer below already uses — invisible to whoever's watching the device
  // versus a hard crash that may not even reboot cleanly.
  const size_t dmaLargestFree = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  if (dmaLargestFree < HEAP_DMA_LARGEST_BLOCK_MIN) {
    LOG_ERR("MAIN", "DMA pool fragmented (largest block %u bytes) — restarting before it fails outright",
           static_cast<unsigned>(dmaLargestFree));
    silentRestart();
  }

  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();
    powerManager.setPowerSaving(false);
  }

  if (millis() >= allowSleepAt) {
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
      // Hold-to-sleep, tap-for-something-else — matches the physical convention of "hold power to
      // turn off" rather than the reverse. (Previously this was inverted: a quick tap slept the
      // device and only a long hold forced a refresh, which is backwards from what a power
      // button is expected to do.) The long-press action is fixed (always Sleep); only the short
      // tap is configurable — see RubySettings::powerShortPressAction.
      const bool wasLongPress = mappedInputManager.getHeldTime() >= POWER_LONG_PRESS_MS;
      if (wasLongPress) {
        enterDeepSleep();
      } else {
        switch (SETTINGS.powerShortPressAction) {
          case PowerShortPressAction::Screenshot: {
            // Same lock the render task holds while actually drawing (see RenderLock/
            // ActivityManager::renderingMutex) — without it, a screenshot taken while a render is
            // mid-flight could read a half-drawn framebuffer.
            RenderLock lock;
            saveScreenshot(renderer);
            break;
          }
          case PowerShortPressAction::PauseRuby:
            toggleCapturePause();
            // No direct handle here to whatever activity is currently on screen (this handler
            // runs before activityManager.loop() below), so ask for a generic redraw rather than
            // force a specific render kind the way Dashboard's own Pause button does — whichever
            // screen is active picks this up on its own next redraw, same as it would for any
            // other out-of-band state change.
            activityManager.requestUpdate();
            break;
          case PowerShortPressAction::ScreenRefresh:
          default: {
            RenderLock lock;
            renderer.displayBuffer(HalDisplay::FULL_REFRESH);
            break;
          }
        }
      }
      lastActivityTime = millis();
      return;
    }
  }

  const unsigned long defragIntervalMs =
      SETTINGS.rawHandshakeCaptureEnabled ? HEAP_DEFRAG_INTERVAL_MS_RAW_CAPTURE : HEAP_DEFRAG_INTERVAL_MS;
  if (millis() - bootMillis >= defragIntervalMs) {
    silentRestart();
  }

  activityManager.loop();

  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);
    yield();
  } else {
    const bool captureIdle = !wifiSniffer.isRunning() && !bleScanner.isRunning();
    if (captureIdle && millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      powerManager.setPowerSaving(true);
      delay(50);
    } else {
      delay(10);
    }
  }
}
