#include "RubyThoughts.h"

namespace RubyThoughts {

const char* thoughtFor(RubyExpression expression) {
  switch (expression) {
    case RubyExpression::EXCITED:
      return "!! fresh handshake !!";
    case RubyExpression::CURIOUS:
      return "ooh, what's this OUI?";
    case RubyExpression::CONTENT:
      return "channels: nominal o_o";
    case RubyExpression::BORED:
      return "no bytes... send help";
    case RubyExpression::LONELY:
      return "netstat -a: empty :(";
    case RubyExpression::SLEEPING:
      return "";
  }
  return "";
}

namespace {
uint8_t clampVariant(uint8_t variant) { return variant % kVariantCount; }
}  // namespace

const char* speechForNewDevice(LogRecordType type, uint8_t variant) {
  static const char* const kApLines[kVariantCount] = {
      "new AP spotted!",     "SSID logged, nice",          "ooh, fresh beacon",
      "another AP joins",    "beacon frame, noted",        "adding to my little black book",
      "channel's got company", "AP++, dopamine++",         "sudo add-ap --silent",
  };
  static const char* const kClientLines[kVariantCount] = {
      "new client, hi there", "probe req noted",         "who dis? logging...",
      "MAC, meet my db",      "someone's phone said hi", "randomized MAC? cute try",
      "client joins the party", "logged, no cap needed", "sniffing you softly",
  };
  static const char* const kBleLines[kVariantCount] = {
      "BLE device found!", "bluetooth blip~",        "GATT is that?",
      "advertising packet, yum", "BLE beacon, added", "another gadget nearby",
      "low energy, high curiosity", "MAC randomization? lol ok", "ping received, logging",
  };

  switch (type) {
    case LogRecordType::WifiAp:
      return kApLines[clampVariant(variant)];
    case LogRecordType::WifiClient:
      return kClientLines[clampVariant(variant)];
    case LogRecordType::BleDevice:
      return kBleLines[clampVariant(variant)];
    case LogRecordType::WifiHandshake:
      return speechForHandshake(variant);
  }
  return "";
}

const char* speechForHandshake(uint8_t variant) {
  static const char* const kLines[kVariantCount] = {
      "EAPOL captured!!", "got the 4-way \\o/", "handshake++, nice",
      "M1-M4, all mine now", "PMKID or bust, got it", "crackable? we'll see",
      "4-way, 1 winner: me", "handshake secured, gg", "EAPOL frames, om nom",
  };
  return kLines[clampVariant(variant)];
}

const char* speechForLevelUp(uint8_t variant) {
  static const char* const kLines[kVariantCount] = {
      "leveling up, brb",  "xp go brrr",           "lvl up! flexing rn",
      "new level unlocked", "achievement: exists", "stonks: my level",
      "sudo level-up --force", "xp bar go brr again", "ding! (that's a level up)",
  };
  return kLines[clampVariant(variant)];
}

const char* speechForDeauthAlert(uint8_t variant) {
  static const char* const kLines[kVariantCount] = {
      "wait... deauth?!",  "not cool, not cool",     "who's spamming frames",
      "deauth spotted, sus", "someone's being rude", "802.11 chaos detected",
      "reason code: rude", "frames flying, not mine", "that's not cash money",
  };
  return kLines[clampVariant(variant)];
}

}  // namespace RubyThoughts
