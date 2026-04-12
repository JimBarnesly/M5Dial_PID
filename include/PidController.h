#pragma once

class PidController {
public:
  void begin(float kp, float ki, float kd);
  void setTunings(float kp, float ki, float kd);
  void reset();
  float compute(float setpoint, float input, float dtSeconds);
  float kp() const { return _kp; }
  float ki() const { return _ki; }
  float kd() const { return _kd; }

private:
  float _kp {0};
  float _ki {0};
  float _kd {0};
  float _integral {0};
  float _lastInput {0};
  bool _hasLast {false};
};
