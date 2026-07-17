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
#include <esp_sleep.h>
#include <esp_system.h>

#include <ctime>
#include <sys/time.h>  // struct timeval / settimeofday() — not part of <ctime>

#include "AppVersion.h"
#include "BleScanner.h"
#include "EncryptedLog.h"
#include "MappedInputManager.h"
#include "RecentSightings.h"
#include "RubyAppState.h"
#include "RubySettings.h"
#include "SignalCatalog.h"
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
// Deliberately longer than a simple debounce: the device rides around in a bag/pocket while
// capturing, and a short jostle against the power button should not wake (and start refreshing
// the display / burning battery) every time it gets bumped.
constexpr unsigned long POWER_BUTTON_WAKE_DEBOUNCE_MS = 500;
constexpr unsigned long POWER_LONG_PRESS_MS = 1200;

}  // namespace

RTC_NOINIT_ATTR uint32_t silentRebootMagic;
static unsigned long allowSleepAt = 0;
static unsigned long bootMillis = 0;

void silentRestart() {
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (heap defrag)");
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
    wifiSniffer.start(WifiSniffer::kAllChannels, WifiSniffer::kAllChannelsCount, SETTINGS.wifiChannelDwellMs);
  }
  if (SETTINGS.bleSniffEnabled) {
    bleScanner.start();
  }
}

void enterDeepSleep() {
  HalPowerManager::Lock powerLock;

  wifiSniffer.stop();
  bleScanner.stop();

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
  LOG_DBG("MAIN", "Entering deep sleep");
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

  // Portrait (480x800 logical) — this is the orientation the X3/X4's physical buttons are
  // actually laid out for. Landscape rotates the logical screen 90° from where Back/Confirm/
  // Left/Right/Up/Down physically sit on the case; upstream CrossPlant handles that with an
  // orientation-aware button remapping layer in MappedInputManager that this port deliberately
  // dropped as reader-specific complexity. Rather than resurrect that layer to chase a landscape
  // dashboard, staying in the native orientation keeps on-screen button hints correct for free.
  renderer.setOrientation(GfxRenderer::Portrait);

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

  if (!isSilentReboot) {
    activityManager.goToBoot();
  }

  SIGNAL_CATALOG.loadFromFile();
  RUBY.begin();
  if (!encryptedLog.begin()) {
    LOG_ERR("MAIN", "Encrypted log failed to initialize — captures will not be persisted to disk");
  }

  wifiSniffer.setObservationCallback([](const WifiObservation& obs) {
    const LogRecordType type = mapWifiKindToLogType(obs.kind);
    encryptedLog.appendWifi(obs, type);
    SIGNAL_CATALOG.observeWifi(obs);
    recentSightings.recordWifi(obs, type);
  });
  bleScanner.setObservationCallback([](const BleObservation& obs) {
    encryptedLog.appendBle(obs);
    SIGNAL_CATALOG.observeBle(obs);
    recentSightings.recordBle(obs);
  });
  SIGNAL_CATALOG.setNewUniqueCallback([](RfEventType type) { RUBY.onSignalEvent(type); });

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

  if (Serial && millis() - lastMemPrint >= 15000) {
    LOG_INF("MEM", "Free: %d bytes, MinFree: %d bytes | WiFi frames dropped: %lu", ESP.getFreeHeap(),
            ESP.getMinFreeHeap(), static_cast<unsigned long>(wifiSniffer.framesDropped()));
    lastMemPrint = millis();
  }

  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();
    powerManager.setPowerSaving(false);
  }

  if (millis() >= allowSleepAt) {
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
      // Hold-to-sleep, tap-to-refresh — matches the physical convention of "hold power to turn
      // off" rather than the reverse. (Previously this was inverted: a quick tap slept the
      // device and only a long hold forced a refresh, which is backwards from what a power
      // button is expected to do.)
      const bool wasLongPress = mappedInputManager.getHeldTime() >= POWER_LONG_PRESS_MS;
      if (wasLongPress) {
        enterDeepSleep();
      } else {
        RenderLock lock;
        renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      }
      lastActivityTime = millis();
      return;
    }
  }

  if (millis() - bootMillis >= HEAP_DEFRAG_INTERVAL_MS) {
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
