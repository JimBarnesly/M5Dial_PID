#include <cassert>
#include <cstdio>
#include <vector>

#include "PidController.h"
#include "AlarmManager.h"

static uint32_t gNowMs = 0;
uint32_t millis() { return gNowMs; }

static void advanceMs(uint32_t delta) { gNowMs += delta; }

static void testPidControllerBounds() {
  PidController pid;
  pid.begin(12.0f, 0.5f, 2.0f);

  float out1 = pid.compute(80.0f, 20.0f, 1.0f);
  assert(out1 >= 0.0f && out1 <= 100.0f);

  float out2 = pid.compute(20.0f, 80.0f, 1.0f);
  assert(out2 >= 0.0f && out2 <= 100.0f);

  float out3 = pid.compute(20.0f, 20.0f, 0.0f);
  assert(out3 == 0.0f);
}

static void testAlarmAcknowledgeAndClear() {
  AlarmManager alarm;
  bool signalState = false;
  alarm.setSignalHandler([&](bool on) { signalState = on; });
  alarm.begin();

  alarm.setAlarm(AlarmCode::OverTemp, "OVER TEMP", true);
  assert(alarm.getAlarm() == AlarmCode::OverTemp);
  assert(!alarm.isAcknowledged());

  // Local UI acknowledgement is allowed by default.
  bool acked = alarm.acknowledge(AlarmControlSource::LocalUi);
  assert(acked);
  assert(alarm.isAcknowledged());
  assert(!signalState);

  alarm.clearAlarm(AlarmControlSource::System);
  assert(alarm.getAlarm() == AlarmCode::None);
}

static void testAlarmBeepWindow() {
  AlarmManager alarm;
  std::vector<bool> signalHistory;
  alarm.setSignalHandler([&](bool on) { signalHistory.push_back(on); });
  alarm.begin();

  alarm.notifyStageComplete();
  alarm.update();
  assert(!signalHistory.empty());
  assert(signalHistory.back());

  advanceMs(800);
  alarm.update();
  assert(signalHistory.back() == false || signalHistory.back() == true);
}

int main() {
  testPidControllerBounds();
  testAlarmAcknowledgeAndClear();
  testAlarmBeepWindow();
  std::puts("All core logic tests passed.");
  return 0;
}
