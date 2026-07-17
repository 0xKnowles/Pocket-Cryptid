#pragma once

#include <cstddef>
#include <cstdint>

// Growth stages. Unlike CrossPlant's pet (which decays if neglected), the cryptid never dies —
// it only goes quiet (see CryptidMood) when the RF environment around it has been empty for a
// while. It is a data-eating creature, not a needy one: the worst that happens is it gets bored.
enum class CryptidStage : uint8_t {
  DORMANT = 0,  // pre-hatch: a flicker of static, no shape yet
  LARVA = 1,
  WRAITH = 2,
  STALKER = 3,
  APEX = 4,     // "The Relay" — final form
};

// Visual/behavioral mood, derived each tick from how recently a *new* unique device was logged
// (not just any traffic — the creature is hungry for novelty, not noise).
enum class CryptidMood : uint8_t {
  THRIVING,   // fed within the last few minutes
  CONTENT,    // fed within the last ~20 minutes
  RESTLESS,   // getting hungry — flickers/glitches more
  STARVING,   // long RF drought — mostly still, occasional twitch
  ASLEEP,     // device is on the sleep screen; not RF-driven
};

// Persistent state, serialized via CryptidManager (PersistableStore<CryptidManager>) to
// /.pocketcryptid/cryptid_state.json. Deliberately small — there is no care-mistake tracking,
// no currency, no shop; growth is a pure function of SignalCatalog's lifetime unique counts.
struct CryptidState {
  bool initialized = false;
  CryptidStage stage = CryptidStage::DORMANT;
  uint32_t xp = 0;                  // cumulative, monotonic — never spent, only ever grows
  uint32_t birthUnixTime = 0;       // 0 if the wall clock had never been set at hatch time
  uint32_t totalUniqueCaptures = 0;  // xp-generating events across the creature's lifetime
  uint16_t unlockedLoreCount = 0;   // how many LoreActivity entries have been revealed so far
  char designation[16] = {};       // cosmetic ID shown in the UI, derived from the chip's MAC

  bool exists() const { return initialized; }
};

namespace CryptidConfig {
// Cumulative XP required to *be at* each stage (index == CryptidStage value).
constexpr uint32_t kStageXpThresholds[] = {0, 30, 150, 600, 2000};
constexpr size_t kStageCount = sizeof(kStageXpThresholds) / sizeof(kStageXpThresholds[0]);

// XP granted per first-ever-seen event. Handshakes are rare and hard-won, so they're worth far
// more than a passing probe request.
constexpr uint32_t kXpNewWifiAp = 2;
constexpr uint32_t kXpNewWifiClient = 3;
constexpr uint32_t kXpNewBleDevice = 2;
constexpr uint32_t kXpHandshakeCaptured = 15;

// Mood thresholds, milliseconds since the last XP-granting capture.
constexpr unsigned long kMoodThrivingMs = 5UL * 60 * 1000;
constexpr unsigned long kMoodContentMs = 20UL * 60 * 1000;
constexpr unsigned long kMoodRestlessMs = 60UL * 60 * 1000;
// Anything beyond kMoodRestlessMs is STARVING.

constexpr const char* kStatePath = "/.pocketcryptid/cryptid_state.json";
}  // namespace CryptidConfig
