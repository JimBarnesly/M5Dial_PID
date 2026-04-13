#include "platform/m5dial/M5DialDigitalOut.h"

M5DialDigitalOut::M5DialDigitalOut(uint8_t pin) : _pin(pin) {}

void M5DialDigitalOut::begin() {
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void M5DialDigitalOut::set(bool on) {
  digitalWrite(_pin, on ? HIGH : LOW);
}
