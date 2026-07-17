#pragma once
#include <HalGPIO.h>

#include <cstddef>

// Thin, fixed mapping from logical buttons to the device's physical buttons. Upstream CrossPlant
// has an elaborate remappable/orientation-aware layer here (reader page-turn side buttons,
// front-button remap, power-as-confirm fallback for one-handed reading); none of that applies to
// a fixed-orientation stats dashboard, so this is mostly a direct passthrough plus small
// suppress-next-release latches for the same "don't double-fire on the button that closed this
// screen" pattern Activity::finishAfterBackPress() relies on.
//
// One correction lives here: HalGPIO::BTN_LEFT/BTN_RIGHT are swapped relative to the physical
// case on real X3/X4 hardware — confirmed against a flashed device where Back/Confirm (the outer
// two of the four front buttons) landed correctly but the two middle buttons opened the screen
// labeled for the *other* one. See hardwareIndex().
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power };
  static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::Power) + 1;

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
  void suppressNextBackRelease() { suppressBackRelease = true; }
  void suppressNextConfirmRelease() { suppressConfirmRelease = true; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const { return gpio.wasAnyPressed(); }
  bool wasAnyReleased() const { return gpio.wasAnyReleased(); }
  unsigned long getHeldTime() const { return gpio.getHeldTime(); }

 private:
  HalGPIO& gpio;
  mutable bool suppressBackRelease = false;
  mutable bool suppressConfirmRelease = false;

  static uint8_t hardwareIndex(Button button);
};
