#include "DeauthDetector.h"

#include <Arduino.h>

DeauthDetector deauthDetector;

void DeauthDetector::onObservation(const WifiObservation& obs) {
  if (obs.kind != WifiFrameKind::Deauth && obs.kind != WifiFrameKind::Disassoc) return;

  lastBssid = obs.bssid;
  if (obs.kind == WifiFrameKind::Deauth) {
    totalDeauth++;
  } else {
    totalDisassoc++;
  }

  const unsigned long now = millis();
  if (now - windowStartMs > kSpikeWindowMs) {
    windowStartMs = now;
    windowCount = 0;
  }
  windowCount++;
  if (windowCount >= kSpikeThreshold) {
    alertUntilMs = now + kAlertDurationMs;
    windowCount = 0;
    windowStartMs = now;
  }
}

bool DeauthDetector::alertActive() const { return millis() < alertUntilMs; }
