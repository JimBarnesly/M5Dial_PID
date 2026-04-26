#pragma once

#include <Arduino.h>

enum class MenuScreenId : uint8_t {
  Root = 0,
  Control,
  Alarm,
  AlarmLog,
  PidSettings,
  Network,
  Integration,
  Device,
  Diagnostics
};

enum class MenuItemKind : uint8_t {
  Submenu = 0,
  Action,
  Value,
  ReadOnly,
  Back,
  Exit
};

enum class MenuItemId : uint16_t {
  None = 0,

  RootControl,
  RootAlarm,
  RootPidSettings,
  RootNetwork,
  RootIntegration,
  RootDevice,
  RootDiagnostics,
  RootExit,

  ControlLocalSetpoint,
  ControlEnable,
  ControlLocalOverride,
  ControlMinLimit,
  ControlMaxLimit,
  ControlLowAlarm,
  ControlHighAlarm,
  ControlStopRun,
  ControlBack,

  AlarmAll,
  AlarmSensorFault,
  AlarmOverTemp,
  AlarmHeatingIneffective,
  AlarmMqttOffline,
  AlarmLowProcessTemp,
  AlarmHighProcessTemp,
  AlarmLogMenu,
  AlarmBack,

  AlarmLogClearAll,
  AlarmLogBack,
  AlarmLogEntry0,
  AlarmLogEntry1,
  AlarmLogEntry2,
  AlarmLogEntry3,
  AlarmLogEntry4,
  AlarmLogEntry5,
  AlarmLogEntry6,
  AlarmLogEntry7,

  PidKp,
  PidKi,
  PidKd,
  PidDirection,
  PidOutputLimit,
  PidAutotune,
  PidBack,

  NetworkWifiStatus,
  NetworkIpAddress,
  NetworkBrokerHost,
  NetworkBrokerPort,
  NetworkClearWifi,
  NetworkBack,

  IntegrationSystemName,
  IntegrationControllerId,
  IntegrationControllerLink,
  IntegrationUnpair,
  IntegrationBack,

  DeviceBrightness,
  DeviceBuzzer,
  DeviceCalibrationOffset,
  DeviceDeviceId,
  DeviceFirmware,
  DeviceBack,

  DiagnosticsAuthority,
  DiagnosticsController,
  DiagnosticsProbeA,
  DiagnosticsProbeB,
  DiagnosticsPidLive,
  DiagnosticsHeat,
  DiagnosticsBack
};

struct MenuItemDefinition {
  MenuItemId id;
  const char* label;
  MenuItemKind kind;
  MenuScreenId target;
};

struct MenuScreenDefinition {
  MenuScreenId id;
  const char* title;
  const MenuItemDefinition* items;
  uint8_t itemCount;
};

struct MenuRenderItem {
  MenuItemId id {MenuItemId::None};
  MenuItemKind kind {MenuItemKind::ReadOnly};
  char label[28] {""};
  char value[48] {""};
  bool selected {false};
};

struct MenuRenderState {
  char title[28] {""};
  bool editing {false};
  uint8_t itemCount {0};
  uint8_t selectedIndex {0};
  MenuRenderItem items[10];
};

using MenuValueFormatter = void (*)(MenuItemId id, MenuRenderItem& item, void* context);

class MenuSystem {
public:
  void reset();
  bool navigate(int32_t diff);
  bool back();
  bool enter(MenuScreenId screen);
  void setEditing(bool editing);

  bool isEditing() const { return _editing; }
  uint8_t depth() const { return _depth; }
  MenuScreenId currentScreenId() const;
  const MenuScreenDefinition& currentScreen() const;
  const MenuItemDefinition& currentItem() const;
  uint8_t selectedIndex() const;
  void buildRenderState(MenuRenderState& out, MenuValueFormatter formatter, void* context) const;

private:
  static const MenuScreenDefinition& definitionFor(MenuScreenId id);
  uint8_t currentDepthIndex() const;

  MenuScreenId _stack[8] {MenuScreenId::Root};
  uint8_t _selectedIndex[8] {0};
  uint8_t _depth {1};
  bool _editing {false};
};
