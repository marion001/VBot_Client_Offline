#pragma once
#include <Arduino.h>

enum ButtonEvent {
  BTN_NONE = 0,
  BTN_SINGLE_CLICK,
  BTN_DOUBLE_CLICK,
  BTN_LONG_PRESS_START
};

class SimpleButton {
private:
  uint8_t pin;
  bool activeLow;
  uint32_t debounceMs;
  uint32_t doubleClickMs;
  uint32_t longPressMs;

  int lastState;
  int stableState;
  uint32_t lastDebounceTime;
  uint32_t pressStartTime;
  uint32_t releaseTime;
  uint8_t clickCount;
  bool longPressTriggered;

public:
  SimpleButton(uint8_t gpio, bool isPullUp = true, uint32_t debounce = 45, uint32_t dblClick = 320, uint32_t longPress = 1800)
    : pin(gpio), activeLow(isPullUp), debounceMs(debounce), doubleClickMs(dblClick), longPressMs(longPress),
      lastState(HIGH), stableState(HIGH), lastDebounceTime(0), pressStartTime(0), releaseTime(0),
      clickCount(0), longPressTriggered(false) {}

  void setPin(uint8_t gpio) {
    pin = gpio;
  }

  void begin() {
    pinMode(pin, activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    int r = digitalRead(pin);
    lastState = r;
    stableState = r;
  }

  ButtonEvent update() {
    int reading = digitalRead(pin);
    uint32_t now = millis();
    ButtonEvent event = BTN_NONE;

    if (reading != lastState) {
      lastDebounceTime = now;
      lastState = reading;
    }

    if ((now - lastDebounceTime) > debounceMs) {
      if (reading != stableState) {
        stableState = reading;

        bool isPressed = activeLow ? (stableState == LOW) : (stableState == HIGH);
        if (isPressed) {
          pressStartTime = now;
          longPressTriggered = false;
        } else {
          // Vừa thả nút
          if (pressStartTime != 0) {
            uint32_t pressDuration = now - pressStartTime;
            pressStartTime = 0;
            if (!longPressTriggered && pressDuration >= debounceMs) {
              clickCount++;
              releaseTime = now;
            }
          }
        }
      }
    }

    // Kiểm tra nhấn giữ (Long Press) trong khi vẫn đang đè nút
    bool currentlyPressed = activeLow ? (stableState == LOW) : (stableState == HIGH);
    if (currentlyPressed && pressStartTime != 0 && !longPressTriggered) {
      if (now - pressStartTime >= longPressMs) {
        longPressTriggered = true;
        clickCount = 0; // Hủy click nếu đã đạt thời gian nhấn giữ
        return BTN_LONG_PRESS_START;
      }
    }

    // Kiểm tra Single Click hoặc Double Click sau khi đã thả nút và hết khoảng chờ
    if (clickCount > 0 && !currentlyPressed) {
      if (clickCount >= 2) {
        clickCount = 0;
        event = BTN_DOUBLE_CLICK;
      } else if (now - releaseTime > doubleClickMs) {
        clickCount = 0;
        event = BTN_SINGLE_CLICK;
      }
    }

    return event;
  }
};
