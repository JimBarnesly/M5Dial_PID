#include "PidController.h"
#include <Arduino.h>

void PidController::begin(float kp, float ki, float kd) {
  _kp = kp;
  _ki = ki;
  _kd = kd;
  reset();
}

void PidController::reset() {
  _integral = 0.0f;
  _lastInput = 0.0f;
  _hasLast = false;
}

float PidController::compute(float setpoint, float input, float dtSeconds) {
  if (dtSeconds <= 0.0f) return 0.0f;

  const float error = setpoint - input;

  _integral += error * dtSeconds;
  _integral = constrain(_integral, -100.0f / max(_ki, 0.001f), 100.0f / max(_ki, 0.001f));

  float derivative = 0.0f;
  if (_hasLast) {
    derivative = -(input - _lastInput) / dtSeconds;  // derivative on measurement
  }

  float output = _kp * error + _ki * _integral + _kd * derivative;
  output = constrain(output, 0.0f, 100.0f);

  if (output == 0.0f || output == 100.0f) {
    _integral -= error * dtSeconds * 0.5f; // mild anti-windup backoff
  }

  _lastInput = input;
  _hasLast = true;
  return output;
}
