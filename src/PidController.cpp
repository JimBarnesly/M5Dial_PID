#include "PidController.h"
#include <Arduino.h>

void PidController::begin(float kp, float ki, float kd) {
  setTunings(kp, ki, kd);
  reset();
}

void PidController::setTunings(float kp, float ki, float kd) {
  _kp = max(0.0f, kp);
  _ki = max(0.0f, ki);
  _kd = max(0.0f, kd);
}

void PidController::setReverseActing(bool reverseActing) {
  _reverseActing = reverseActing;
}

void PidController::reset() {
  _integral = 0.0f;
  _lastInput = 0.0f;
  _hasLast = false;
}

float PidController::compute(float setpoint, float input, float dtSeconds) {
  if (dtSeconds <= 0.0f) return 0.0f;

  const float direction = _reverseActing ? -1.0f : 1.0f;
  const float error = (setpoint - input) * direction;

  _integral += error * dtSeconds;
  _integral = constrain(_integral, -100.0f / max(_ki, 0.001f), 100.0f / max(_ki, 0.001f));

  float derivative = 0.0f;
  if (_hasLast) {
    derivative = -direction * (input - _lastInput) / dtSeconds;  // derivative on measurement
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
