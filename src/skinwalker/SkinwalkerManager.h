#pragma once

#include <PersistableStore.h>

#include <cstddef>

#include "SignalCatalog.h"
#include "SkinwalkerBehavior.h"
#include "SkinwalkerState.h"

// Owns the skinwalker's persistent state and the small bit of live session state (capture/
// handshake timers, animation clock) needed to render it. Wire it up once at boot:
//
//   SKINWALKER.begin();
//   SIGNAL_CATALOG.setNewUniqueCallback([](RfEventType t) { SKINWALKER.onSignalEvent(t); });
//
// and call SKINWALKER.tick() from the main loop alongside SIGNAL_CATALOG.tick().
class SkinwalkerManager : public PersistableStore<SkinwalkerManager> {
  friend class PersistableStore<SkinwalkerManager>;

 public:
  void begin();

  // Registered as SignalCatalog's new-unique callback; bumps the lifetime capture count, may
  // unlock a lore entry, and refreshes the timers that drive currentExpression().
  void onSignalEvent(RfEventType type);

  // Debounced persistence, call once per loop iteration.
  void tick();

  const SkinwalkerState& getState() const { return state; }

  // deviceSleeping short-circuits straight to SLEEPING regardless of activity timers — used by
  // SleepActivity so the creature visibly "goes quiet" the instant the screen does.
  SkinwalkerExpression currentExpression(bool deviceSleeping) const;

  // True exactly once, the first time this is polled after a handshake capture. Callers
  // (DashboardActivity) use this to trigger a one-off "flash" banner instead of a silent update.
  bool consumeJustCapturedHandshake();

  // 2-frame idle loop, ~1.2s per frame — slow enough to stay believable as an e-ink partial
  // refresh cadence, fast enough to read as "alive" rather than a slideshow.
  uint8_t animFrame() const;

  // Lore entries unlock progressively with lifetime captures; see SkinwalkerManager.cpp for the
  // table. Returns nullptr for an out-of-range index.
  static size_t loreEntryCount();
  const char* loreEntry(size_t index) const;  // nullptr if index >= unlocked count

  static const char* getFilePath() { return SkinwalkerConfig::kStatePath; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  SkinwalkerManager() = default;

  SkinwalkerState state;
  unsigned long lastCaptureMillis = 0;
  unsigned long lastHandshakeMillis = 0;
  unsigned long birthMillis = 0;
  bool dirty = false;
  bool justCapturedHandshake = false;
  unsigned long lastSaveMs = 0;
};

#define SKINWALKER SkinwalkerManager::getInstance()
