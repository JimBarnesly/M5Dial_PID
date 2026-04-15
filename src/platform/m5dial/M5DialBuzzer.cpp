#include "platform/m5dial/M5DialBuzzer.h"

M5DialBuzzer::M5DialBuzzer(uint8_t pin) : _pin(pin) {}

void M5DialBuzzer::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void M5DialBuzzer::set(bool on) {
  digitalWrite(_pin, on ? HIGH : LOW);
}
