#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

using std::isnan;

using byte = uint8_t;

uint32_t millis();
inline void delay(uint32_t) {}

template <typename T>
inline T constrain(T x, T a, T b) {
  return std::min(std::max(x, a), b);
}

template <typename T>
inline T min(T a, T b) { return std::min(a, b); }

template <typename T>
inline T max(T a, T b) { return std::max(a, b); }
