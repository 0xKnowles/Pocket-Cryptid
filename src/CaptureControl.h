#pragma once

// Session-only pause toggle for WiFi/BLE capture, reachable from DashboardActivity's Left
// button (same button pauses and resumes). Deliberately not persisted — a fresh boot always
// starts unpaused regardless of how the device was left, same as raw capture/active deauth
// resuming from RubySettings rather than from whatever transient state was in effect before a
// reboot.
//
// Pausing stops WifiSniffer and BleScanner outright (not just gating their callbacks), so it
// also has the side effect of halting DeauthEngine (driven entirely by WifiSniffer's observation
// stream) and stopping all SD/EncryptedLog write activity for as long as it's paused. Resuming
// respects RubySettings::wifiSniffEnabled/bleSniffEnabled — it won't force either radio back on
// if the owner had it turned off in Settings before pausing.
bool captureIsPaused();
void toggleCapturePause();
