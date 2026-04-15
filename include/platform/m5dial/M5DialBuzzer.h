#pragma once

#include <Arduino.h>

class M5DialBuzzer {
public:
  explicit M5DialBuzzer(uint8_t pin);
  void begin();
  void set(bool on);

private:
  uint8_t _pin;
};
