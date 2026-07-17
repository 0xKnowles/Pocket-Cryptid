#include "CaptureControl.h"

#include <Logging.h>

#include "BleScanner.h"
#include "DeauthEngine.h"
#include "RubySettings.h"
#include "WifiSniffer.h"

namespace {
bool paused = false;
}

bool captureIsPaused() { return paused; }

void toggleCapturePause() {
  paused = !paused;
  if (paused) {
    LOG_INF("CAPTURE", "Paused");
    wifiSniffer.stop();
    bleScanner.stop();
  } else {
    LOG_INF("CAPTURE", "Resumed");
    if (SETTINGS.wifiSniffEnabled) {
      wifiSniffer.start(WifiSniffer::kAllChannels, WifiSniffer::kAllChannelsCount, SETTINGS.wifiChannelDwellMs,
                        SETTINGS.rawHandshakeCaptureEnabled);
      deauthEngine.setEnabled(SETTINGS.activeDeauthEnabled && SETTINGS.rawHandshakeCaptureEnabled);
    }
    if (SETTINGS.bleSniffEnabled) {
      bleScanner.start();
    }
  }
}
