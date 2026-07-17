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

  // Registered as SignalCatalog's new-unique callback; bumps the lifetime capture count, may
  // unlock a lore entry, and refreshes the timers that drive currentExpression().
  void onSignalEvent(RfEventType type);

  // Debounced persistence, call once per loop iteration.
  void tick();

  const RubyState& getState() const { return state; }

  // deviceSleeping short-circuits straight to SLEEPING regardless of activity timers — used by
  // SleepActivity so the creature visibly "goes quiet" the instant the screen does.
  RubyExpression currentExpression(bool deviceSleeping) const;

  // True exactly once, the first time this is polled after a handshake capture. Callers
  // (DashboardActivity) use this to trigger a one-off "flash" banner instead of a silent update.
  bool consumeJustCapturedHandshake();

  // 2-frame idle loop, ~1.2s per frame — slow enough to stay believable as an e-ink partial
  // refresh cadence, fast enough to read as "alive" rather than a slideshow.
  uint8_t animFrame() const;

  // Lore entries unlock progressively with lifetime captures; see RubyManager.cpp for the
  // table. Returns nullptr for an out-of-range index.
  static size_t loreEntryCount();
  const char* loreEntry(size_t index) const;  // nullptr if index >= unlocked count

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
};

#define RUBY RubyManager::getInstance()
