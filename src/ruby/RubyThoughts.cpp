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
  static const char* const kApLines[kVariantCount] = {"new AP spotted!", "SSID logged, nice", "ooh, fresh beacon"};
  static const char* const kClientLines[kVariantCount] = {"new client, hi there", "probe req noted",
                                                          "who dis? logging..."};
  static const char* const kBleLines[kVariantCount] = {"BLE device found!", "bluetooth blip~", "GATT is that?"};

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
  static const char* const kLines[kVariantCount] = {"EAPOL captured!!", "got the 4-way \\o/", "handshake++, nice"};
  return kLines[clampVariant(variant)];
}

const char* speechForLevelUp(uint8_t variant) {
  static const char* const kLines[kVariantCount] = {"leveling up, brb", "xp go brrr", "lvl up! flexing rn"};
  return kLines[clampVariant(variant)];
}

const char* speechForDeauthAlert(uint8_t variant) {
  static const char* const kLines[kVariantCount] = {"wait... deauth?!", "not cool, not cool", "who's spamming frames"};
  return kLines[clampVariant(variant)];
}

}  // namespace RubyThoughts
