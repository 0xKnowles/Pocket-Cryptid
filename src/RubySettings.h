#pragma once
#include <PersistableStore.h>

#include <cstdint>

// What a short tap of the physical Power button does (a long hold always sleeps the device — see
// main.cpp — that part isn't configurable, only the short-tap action is).
enum class PowerShortPressAction : uint8_t {
  ScreenRefresh = 0,  // manual full ghost-clearing refresh — the long-standing default
  Screenshot = 1,     // dump the current framebuffer to /.ruby/screenshots/*.bmp
  PauseRuby = 2,      // same toggle as Dashboard's own Pause button, usable from any screen
};

// All user-configurable behavior lives here. Deliberately small: there is no theming, no button
// remap, no per-book anything — just the knobs that affect what the radios do and how the log is
// protected. See SettingsActivity for the UI that edits these.
class RubySettings : public PersistableStore<RubySettings> {
  friend class PersistableStore<RubySettings>;

 public:
  bool wifiSniffEnabled = true;

  // Off by default, unlike wifiSniffEnabled above — real-hardware testing (serial log) found this
  // radio fragmenting the DMA-capable pool EncryptedLog's hardware AES needs badly enough,
  // immediately at boot, to trip the DMA-pool circuit breaker (main.cpp) on every single boot —
  // an endless ~1-second restart loop that looks and feels exactly like a hang. A crash-loop guard
  // now forces this back off automatically after 3 consecutive silent restarts, but this radio
  // should still never start on its own; only a deliberate opt-in in Settings should risk it.
  bool bleSniffEnabled = false;

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

  // 0 = hop all 13 channels (the default); 1-13 = lock onto just that one, skipping the hop
  // entirely (see WifiSniffer::channelPlanFor). A handshake completes in well under a second, so
  // hopping across 13 channels means the radio is only actually on any given one ~1/13th of the
  // time — locking to a known target's channel (visible in the live scan behind Settings'
  // Whitelist/Blacklist rows) raises that to 100%, the single biggest lever for catching one.
  uint8_t wifiChannelScope = 0;

  // Biased quarter-hour UTC offset, same convention as upstream HalClock (48 = UTC+0, 0 =
  // UTC-12, 104 = UTC+14). Used only for on-screen time display; log records always store real
  // unix time (or boot-relative time if the clock was never set — see EncryptedLog).
  uint8_t clockUtcOffsetQ = 48;

  // How often (minutes) the dashboard forces a full e-ink refresh to clear accumulated ghosting
  // from the mostly-partial-refresh pet corner. 0 disables.
  uint8_t fullRefreshIntervalMin = 20;

  // See PowerShortPressAction. Stored as its underlying uint8_t in JSON like any other enum here.
  PowerShortPressAction powerShortPressAction = PowerShortPressAction::ScreenRefresh;

  static const char* getFilePath() { return "/.ruby/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  RubySettings() = default;
};

#define SETTINGS RubySettings::getInstance()
