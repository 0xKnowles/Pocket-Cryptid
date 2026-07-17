#include "CrashActivity.h"

#include <GfxRenderer.h>
#include <HalSystem.h>

#include "fontIds.h"
#include "ui/Chrome.h"

void CrashActivity::onEnter() {
  Activity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = "Unknown reason.";
  }
  HalSystem::clearPanic();

  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("CRASH", "Crash screen could not be rendered synchronously");
    requestUpdate();
  }
}

void CrashActivity::loop() {
  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void CrashActivity::render(RenderLock&&) {
  renderer.clearScreen();
  Chrome::drawHeader(renderer, "SOMETHING WOKE IT WRONG");

  const int contentWidth = Chrome::contentRight(renderer) - Chrome::contentLeft();
  const int lineHeight = renderer.getLineHeight(FONT_UI_10_ID);
  int y = Chrome::contentTop();

  const auto descLines =
      renderer.wrappedText(FONT_UI_10_ID, "The firmware restarted unexpectedly. Capture and the log survived — "
                                          "only the current session state was lost.",
                           contentWidth, 6);
  for (const auto& line : descLines) {
    renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, line.c_str());
    y += lineHeight;
  }

  y += 8;
  renderer.drawText(FONT_UI_10_ID, Chrome::contentLeft(), y, "Panic reason:", true, EpdFontFamily::BOLD);
  y += lineHeight + 4;

  const auto panicLines = renderer.wrappedText(FONT_SMALL_ID, panicMessage.c_str(), contentWidth, 8);
  for (const auto& line : panicLines) {
    renderer.drawText(FONT_SMALL_ID, Chrome::contentLeft(), y, line.c_str());
    y += renderer.getLineHeight(FONT_SMALL_ID);
  }

  Chrome::drawFooterHints(renderer, "Continue", nullptr, nullptr, nullptr);
  renderer.displayBuffer();
}
