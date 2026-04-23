#pragma once

#include <Arduino.h>

class M5DialBuzzer {
public:
  explicit M5DialBuzzer(uint8_t pin);
  void begin();
  void set(bool on);
  void playCompletionPattern();
  void update();

private:
  void applyOutput(uint32_t now);
  void startTone(uint16_t frequencyHz);
  void stopTone();

  uint8_t _pin;
  uint32_t _phaseDeadlineMs {0};
  uint8_t _remainingPatternBeeps {0};
  uint16_t _currentFrequencyHz {0};
  bool _alarmOn {false};
  bool _patternActive {false};
  bool _patternToneOn {false};
};
