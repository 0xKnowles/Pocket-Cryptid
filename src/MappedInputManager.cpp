#include "MappedInputManager.h"

uint8_t MappedInputManager::hardwareIndex(Button button) {
  switch (button) {
    case Button::Back:
      return HalGPIO::BTN_BACK;
    case Button::Confirm:
      return HalGPIO::BTN_CONFIRM;
    case Button::Left:
      return HalGPIO::BTN_LEFT;
    case Button::Right:
      return HalGPIO::BTN_RIGHT;
    case Button::Up:
      return HalGPIO::BTN_UP;
    case Button::Down:
      return HalGPIO::BTN_DOWN;
    case Button::Power:
      return HalGPIO::BTN_POWER;
  }
  return HalGPIO::BTN_BACK;
}

bool MappedInputManager::wasPressed(const Button button) const { return gpio.wasPressed(hardwareIndex(button)); }

bool MappedInputManager::wasReleased(const Button button) const {
  if (!gpio.wasReleased(hardwareIndex(button))) return false;

  if (button == Button::Back && suppressBackRelease) {
    suppressBackRelease = false;
    return false;
  }
  if (button == Button::Confirm && suppressConfirmRelease) {
    suppressConfirmRelease = false;
    return false;
  }
  return true;
}

bool MappedInputManager::isPressed(const Button button) const { return gpio.isPressed(hardwareIndex(button)); }
