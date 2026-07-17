#pragma once

#include <PersistableStore.h>

#include <cstddef>

#include "CryptidEvolution.h"
#include "CryptidState.h"
#include "SignalCatalog.h"

// Owns the cryptid's persistent state and the small bit of live session state (mood drought
// timer, animation clock) needed to render it. Wire it up once at boot:
//
//   CRYPTID.begin();
//   SIGNAL_CATALOG.setNewUniqueCallback([](RfEventType t) { CRYPTID.onSignalEvent(t); });
//
// and call CRYPTID.tick() from the main loop alongside SIGNAL_CATALOG.tick().
class CryptidManager : public PersistableStore<CryptidManager> {
  friend class PersistableStore<CryptidManager>;

 public:
  void begin();

  // Registered as SignalCatalog's new-unique callback; grants XP and may advance the stage.
  void onSignalEvent(RfEventType type);

  // Debounced persistence, call once per loop iteration.
  void tick();

  const CryptidState& getState() const { return state; }

  // deviceSleeping short-circuits straight to ASLEEP regardless of drought timing — used by
  // SleepActivity so the creature visibly "goes quiet" the instant the screen does.
  CryptidMood currentMood(bool deviceSleeping) const;

  // True exactly once, the first time this is polled after a stage-up. Callers (DashboardActivity)
  // use this to trigger a one-off "evolution" flourish instead of a silent stage bump.
  bool consumeJustEvolved();

  // 2-frame idle loop, ~1.2s per frame — slow enough to stay believable as an e-ink partial
  // refresh cadence, fast enough to read as "alive" rather than a slideshow.
  uint8_t animFrame() const;

  // Lore entries unlock progressively with XP; see CryptidManager.cpp for the table. Returns
  // nullptr for an out-of-range index.
  static size_t loreEntryCount();
  const char* loreEntry(size_t index) const;  // nullptr if index >= unlocked count

  static const char* getFilePath() { return CryptidConfig::kStatePath; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

 private:
  CryptidManager() = default;

  CryptidState state;
  unsigned long lastCaptureMillis = 0;
  unsigned long birthMillis = 0;
  bool dirty = false;
  bool justEvolved = false;
  unsigned long lastSaveMs = 0;
};

#define CRYPTID CryptidManager::getInstance()
