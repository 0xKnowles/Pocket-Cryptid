#pragma once

#include <PersistableStore.h>

#include <cstddef>

#include "RubyBehavior.h"
#include "RubyState.h"
#include "SignalCatalog.h"

// Owns Ruby's persistent state and the small bit of live session state (capture/
// handshake timers, animation clock) needed to render it. Wire it up once at boot:
//
//   RUBY.begin();
//   SIGNAL_CATALOG.setNewUniqueCallback([](RfEventType t) { RUBY.onSignalEvent(t); });
//
// and call RUBY.tick() from the main loop alongside SIGNAL_CATALOG.tick().
class RubyManager : public PersistableStore<RubyManager> {
  friend class PersistableStore<RubyManager>;

 public:
  void begin();

  // Registered as SignalCatalog's new-unique callback; refreshes the timers that drive
  // currentExpression().
  void onSignalEvent(RfEventType type);

  // Debounced persistence, call once per loop iteration.
  void tick();

  const RubyState& getState() const { return state; }

  // 1-5, derived from state.totalExp — see RubyConfig::levelForExp()/kLevelThresholds.
  uint8_t level() const { return RubyConfig::levelForExp(state.totalExp); }

  // 0.0-1.0 progress from the current level's floor toward the next one; pinned to 1.0 once
  // level() reaches RubyConfig::kMaxLevel, since there's no "next" floor beyond that. Drives the
  // Dashboard header's EXP bar (see DashboardActivity::renderFull()).
  float expProgress() const;

  // Raw numbers behind expProgress(), for the "N/M EXP" label next to the bar: EXP earned since
  // the current level's floor, and EXP needed to cross into the next one. Both 0 at max level.
  void expIntoLevel(uint32_t& intoLevel, uint32_t& neededForLevel) const;

  // deviceSleeping short-circuits straight to SLEEPING regardless of activity timers — used by
  // SleepActivity so the creature visibly "goes quiet" the instant the screen does.
  RubyExpression currentExpression(bool deviceSleeping) const;

  // True exactly once, the first time this is polled after a handshake capture. Callers
  // (DashboardActivity) use this to trigger a one-off "flash" banner instead of a silent update.
  bool consumeJustCapturedHandshake();

  // 2-frame idle loop, ~1.2s per frame — slow enough to stay believable as an e-ink partial
  // refresh cadence, fast enough to read as "alive" rather than a slideshow.
  uint8_t animFrame() const;

  static const char* getFilePath() { return RubyConfig::kStatePath; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  RubyManager() = default;

  RubyState state;
  unsigned long lastCaptureMillis = 0;
  unsigned long lastHandshakeMillis = 0;
  unsigned long birthMillis = 0;
  bool dirty = false;
  bool justCapturedHandshake = false;
  unsigned long lastSaveMs = 0;

  // Ring of recent new-unique-sighting timestamps (any type), used to tell a genuine burst of
  // activity (CURIOUS) apart from ordinary single sightings (CONTENT) — see RubyBehavior's class
  // comment. 0 means "unused slot," never a real timestamp (millis() is only ever 0 in the first
  // instant after boot, well before this could matter).
  unsigned long recentEventTimes[RubyConfig::kRecentEventCapacity] = {};
  size_t recentEventNext = 0;
};

#define RUBY RubyManager::getInstance()
