#include "LoreActivity.h"

#include <GfxRenderer.h>

#include <cstdio>

#include "fontIds.h"
#include "ruby/RubyManager.h"
#include "ui/Chrome.h"

void LoreActivity::onEnter() {
  Activity::onEnter();
  index = RUBY.getState().unlockedLoreCount > 0 ? RUBY.getState().unlockedLoreCount - 1 : 0;
  requestUpdate();
}

void LoreActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  const uint16_t unlocked = RUBY.getState().unlockedLoreCount;
  if (unlocked == 0) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    index = (index == 0) ? unlocked - 1 : index - 1;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    index = (index + 1) % unlocked;
    requestUpdate();
    return;
  }
}

void LoreActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "LORE");

  const uint16_t unlocked = RUBY.getState().unlockedLoreCount;
  const int contentWidth = Chrome::contentRight(renderer) - Chrome::contentLeft();
  int y = Chrome::contentTop() + 20;

  if (unlocked == 0) {
    renderer.drawCenteredText(FONT_UI_10_ID, renderer.getScreenHeight() / 2,
                              "It hasn't told you anything yet. Keep listening.");
  } else {
    char counter[16];
    snprintf(counter, sizeof(counter), "%zu / %u", index + 1, unlocked);
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), Chrome::contentTop(), counter);

    const char* entry = RUBY.loreEntry(index);
    if (entry != nullptr) {
      const auto lines = renderer.wrappedText(FONT_UI_12_ID, entry, contentWidth, 8);
      const int lineHeight = renderer.getLineHeight(FONT_UI_12_ID);
      for (const auto& line : lines) {
        renderer.drawText(FONT_UI_12_ID, Chrome::contentLeft(), y, line.c_str());
        y += lineHeight;
      }
    }
  }

  Chrome::drawFooterHints(renderer, "Home", nullptr, "Prev", "Next");
  renderer.displayBuffer();
}
