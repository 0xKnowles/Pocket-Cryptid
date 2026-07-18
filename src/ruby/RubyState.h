#pragma once

#include <cstddef>
#include <cstdint>

// Ruby's *shape* never changes — she's a fixed, already-fully-formed creature (see
// RubySpriteRenderer) whose *expression* reacts to what it's currently hearing, closer to
// Pwnagotchi's mood faces than to a pet that visibly evolves. EXCITED/CURIOUS are short-lived
// "just found something" flashes; CONTENT/BORED/LONELY are the slower drought-based baseline;
// SLEEPING mirrors the device's own sleep state.
//
// Separately, a small Level 1-5 badge (RubyConfig::levelForExp(), drawn next to the name chip —
// see RubySpriteRenderer::draw()) tracks lifetime progress via `totalExp` below, without touching
// the art at all. EXP comes from two very different-sized rewards, deliberately: a captured
// handshake is worth `kExpPerHandshake` (2000) and a newly-seen unique device (AP/client/BLE) is
// worth `kExpPerUniqueDevice` (1) — chosen so the two contribute comparably despite handshakes
// being roughly 2000x rarer in practice, rather than one utterly swamping the other.
enum class RubyExpression : uint8_t {
  EXCITED,   // a handshake was captured moments ago — the rarest, biggest find
  CURIOUS,   // any new unique device was seen moments ago
  CONTENT,   // steady recent activity, nothing brand new right this second
  BORED,     // it's been quiet for a while
  LONELY,    // it's been quiet for a long while
  SLEEPING,  // device is on the sleep screen; not RF-driven
};

// Persistent state, serialized via RubyManager (PersistableStore<RubyManager>) to
// /.ruby/ruby_state.json. Still deliberately small — no stage, no currency, no shop; `totalExp`
// is the one piece of lifetime progress tracked, purely to drive the Level badge.
struct RubyState {
  bool initialized = false;
  uint32_t birthUnixTime = 0;  // 0 if the wall clock had never been set at hatch time
  char designation[16] = {};   // cosmetic ID shown in the UI, derived from the chip's MAC
  uint32_t totalExp = 0;       // lifetime EXP total — see RubyConfig::levelForExp() below

  bool exists() const { return initialized; }
};

namespace RubyConfig {
// Expression thresholds, milliseconds since the relevant kind of capture.
constexpr unsigned long kExcitedWindowMs = 30UL * 1000;         // handshake glow
constexpr unsigned long kCuriousWindowMs = 90UL * 1000;         // "it just found something" blip
constexpr unsigned long kContentWindowMs = 20UL * 60 * 1000;    // steady recent activity
constexpr unsigned long kBoredWindowMs = 60UL * 60 * 1000;      // getting quiet
// Anything beyond kBoredWindowMs is LONELY.

// CURIOUS used to fire off any single new-unique sighting within kCuriousWindowMs — in a
// target-rich environment (a busy street, an apartment building) a new AP/client/BLE device can
// realistically show up more often than every 90 seconds indefinitely, which pinned the
// expression on CURIOUS permanently instead of it ever reading as the steady CONTENT baseline.
// Requiring a real burst (this many new-unique sightings within the window, not just one) makes
// CURIOUS mean what it's supposed to: "something just picked up," not "the radio is still on."
constexpr uint8_t kCuriousBurstThreshold = 3;
// Ring size for tracking recent new-unique-sighting timestamps (see RubyManager) — only needs to
// comfortably outlast kCuriousBurstThreshold entries within kCuriousWindowMs, not a long history.
constexpr size_t kRecentEventCapacity = 8;

constexpr const char* kStatePath = "/.ruby/ruby_state.json";

// EXP awarded per event (see RubyManager::onSignalEvent()). A handshake is worth 2000x a single
// unique device — matching the real-world ratio (roughly 1 handshake per ~2000 unique devices
// seen) means, over a typical session, the two contribute *comparably* to leveling rather than
// unique-device sightings (common) drowning out handshakes (rare) or vice versa.
constexpr uint32_t kExpPerUniqueDevice = 1;
constexpr uint32_t kExpPerHandshake = 2000;

// Level 1-5. kLevelThresholds[i] is the EXP floor for level i+1 — kLevelThresholds[0] is always 0
// (everyone starts at level 1). 5x the original thresholds (2000/8000/20000/50000) — real overnight
// testing (a 5-hour unattended run) reached level 3 in one sitting, leveling far faster than
// intended for a "lifetime progress" badge. Now takes 5 handshakes (10000 EXP) alone to reach
// level 2, instead of just 1.
constexpr uint8_t kMaxLevel = 5;
constexpr uint32_t kLevelThresholds[kMaxLevel] = {0, 10000, 40000, 100000, 250000};

inline uint8_t levelForExp(uint32_t exp) {
  uint8_t level = 1;
  for (uint8_t i = 0; i < kMaxLevel; i++) {
    if (exp >= kLevelThresholds[i]) level = static_cast<uint8_t>(i + 1);
  }
  return level;
}
}  // namespace RubyConfig
