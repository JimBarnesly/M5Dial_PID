#pragma once

#include <Arduino.h>

class M5DialDigitalOut {
public:
  explicit M5DialDigitalOut(uint8_t pin);
  void begin();
  void set(bool on);

private:
  uint8_t _pin;
};
