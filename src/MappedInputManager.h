#pragma once
#include <HalGPIO.h>

#include <cstddef>

// Thin, fixed mapping from logical buttons to the device's physical buttons. Upstream CrossPlant
// has an elaborate remappable/orientation-aware layer here (reader page-turn side buttons,
// front-button remap, power-as-confirm fallback for one-handed reading); none of that applies to
// a fixed-orientation stats dashboard, so this is a direct passthrough plus small
// suppress-next-release latches for the same "don't double-fire on the button that closed this
// screen" pattern Activity::finishAfterBackPress() relies on.
//
// This used to swap HalGPIO::BTN_LEFT/BTN_RIGHT here, based on one X4 test where the two middle
// front buttons seemed to open each other's screen. That swap contradicted the ADC-ladder
// calibration table in freeink-sdk's InputManager.cpp (real recorded voltages from physically
// pressing BACK/CONF/LEFT/RIGHT on Xteink hardware, which is the ground truth this device's
// button reading is built on), and a later, broader "buttons don't do what they say" report on a
// second X3 unit suggests that swap was the wrong fix for the wrong bug. Reverted back to direct
// passthrough; see hardwareIndex().
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
