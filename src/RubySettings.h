#pragma once
#include <PersistableStore.h>

#include <cstdint>

// All user-configurable behavior lives here. Deliberately small: there is no theming, no button
// remap, no per-book anything — just the knobs that affect what the radios do and how the log is
// protected. See SettingsActivity for the UI that edits these.
class RubySettings : public PersistableStore<RubySettings> {
  friend class PersistableStore<RubySettings>;

 public:
  bool wifiSniffEnabled = true;
  bool bleSniffEnabled = true;

  // Off by default: captures verbatim WPA handshake bytes (ANonce/SNonce/MIC included) to a
  // plaintext .pcap on SD for offline auditing with hashcat/hcxpcapngtool, instead of the
  // metadata-only capture every other setting here governs. See WifiSniffer's class comment and
  // SettingsActivity's on-screen warning before turning this on.
  bool rawHandshakeCaptureEnabled = false;

  // Off by default: transmits 802.11 deauthentication frames (see DeauthEngine) at BSSIDs
  // TargetList allows, to force a handshake instead of waiting for one passively. Real RF
  // interference against whatever it targets — see DeauthEngine's class comment and
  // SettingsActivity's on-screen warning before turning this on. main.cpp additionally refuses
  // to start it unless rawHandshakeCaptureEnabled is also on, since forcing a handshake nobody's
  // capturing verbatim would just be pointless disruption.
  bool activeDeauthEnabled = false;

  // Milliseconds spent on each WiFi channel before hopping to the next (see WifiSniffer). Lower
  // = catches more short-lived probe bursts but misses more beacons per channel; higher = the
  // reverse. 100-1000 is a sane range.
  uint16_t wifiChannelDwellMs = 300;

  // Biased quarter-hour UTC offset, same convention as upstream HalClock (48 = UTC+0, 0 =
  // UTC-12, 104 = UTC+14). Used only for on-screen time display; log records always store real
  // unix time (or boot-relative time if the clock was never set — see EncryptedLog).
  uint8_t clockUtcOffsetQ = 48;

  // How often (minutes) the dashboard forces a full e-ink refresh to clear accumulated ghosting
  // from the mostly-partial-refresh pet corner. 0 disables.
  uint8_t fullRefreshIntervalMin = 20;

  static const char* getFilePath() { return "/.ruby/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  RubySettings() = default;
};

#define SETTINGS RubySettings::getInstance()
