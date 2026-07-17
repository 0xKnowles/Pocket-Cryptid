#pragma once

#include <cstddef>
#include <cstdint>

// Ruby doesn't level up or grow — she's a fixed, already-fully-formed shape (see
// RubySpriteRenderer) whose *expression* reacts to what it's currently hearing, closer to
// Pwnagotchi's mood faces than to a pet that eats XP to evolve. EXCITED/CURIOUS are short-lived
// "just found something" flashes; CONTENT/BORED/LONELY are the slower drought-based baseline;
// SLEEPING mirrors the device's own sleep state.
enum class RubyExpression : uint8_t {
  EXCITED,   // a handshake was captured moments ago — the rarest, biggest find
  CURIOUS,   // any new unique device was seen moments ago
  CONTENT,   // steady recent activity, nothing brand new right this second
  BORED,     // it's been quiet for a while
  LONELY,    // it's been quiet for a long while
  SLEEPING,  // device is on the sleep screen; not RF-driven
};

// Persistent state, serialized via RubyManager (PersistableStore<RubyManager>) to
// /.ruby/ruby_state.json. Deliberately small — there is no XP, no stage, no currency, no shop.
struct RubyState {
  bool initialized = false;
  uint32_t birthUnixTime = 0;  // 0 if the wall clock had never been set at hatch time
  char designation[16] = {};   // cosmetic ID shown in the UI, derived from the chip's MAC

  bool exists() const { return initialized; }
};

namespace RubyConfig {
// Expression thresholds, milliseconds since the relevant kind of capture.
constexpr unsigned long kExcitedWindowMs = 30UL * 1000;         // handshake glow
constexpr unsigned long kCuriousWindowMs = 90UL * 1000;         // "it just found something" blip
constexpr unsigned long kContentWindowMs = 20UL * 60 * 1000;    // steady recent activity
constexpr unsigned long kBoredWindowMs = 60UL * 60 * 1000;      // getting quiet
// Anything beyond kBoredWindowMs is LONELY.

constexpr const char* kStatePath = "/.ruby/ruby_state.json";
}  // namespace RubyConfig
