#include "MenuSystem.h"

#include <cstring>

namespace {
constexpr uint8_t kRootItemCount = 7;
constexpr uint8_t kStatusItemCount = 10;
constexpr uint8_t kControlItemCount = 9;
constexpr uint8_t kAlarmItemCount = 9;
constexpr uint8_t kAlarmLogItemCount = 10;
constexpr uint8_t kPidItemCount = 7;
constexpr uint8_t kNetworkItemCount = 6;
constexpr uint8_t kIntegrationItemCount = 5;
constexpr uint8_t kDeviceItemCount = 7;

const MenuItemDefinition kRootItems[kRootItemCount] = {
  {MenuItemId::RootControl, "Control", MenuItemKind::Submenu, MenuScreenId::Control},
  {MenuItemId::RootAlarm, "Alarm", MenuItemKind::Submenu, MenuScreenId::Alarm},
  {MenuItemId::RootPidSettings, "PID Settings", MenuItemKind::Submenu, MenuScreenId::PidSettings},
  {MenuItemId::RootNetwork, "Network", MenuItemKind::Submenu, MenuScreenId::Network},
  {MenuItemId::RootIntegration, "Integration", MenuItemKind::Submenu, MenuScreenId::Integration},
  {MenuItemId::RootDevice, "Device", MenuItemKind::Submenu, MenuScreenId::Device},
  {MenuItemId::RootExit, "Exit", MenuItemKind::Exit, MenuScreenId::Root}
};

const MenuItemDefinition kStatusItems[kStatusItemCount] = {
  {MenuItemId::StatusOperatingMode, "Operating Mode", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusAuthority, "Authority", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusSystemName, "System Name", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusControllerLink, "Controller Link", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusWifi, "Wi-Fi", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusMqtt, "MQTT", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusProcessVariable, "Process Variable", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusOutput, "Output", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusAlarm, "Alarm", MenuItemKind::ReadOnly, MenuScreenId::Status},
  {MenuItemId::StatusBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kControlItems[kControlItemCount] = {
  {MenuItemId::ControlLocalSetpoint, "Local Setpoint", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlEnable, "Control Enabled", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlLocalOverride, "Temporary Standalone", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlMinLimit, "Min Temp Limit", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlMaxLimit, "Max Temp Limit", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlLowAlarm, "Low Alarm", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlHighAlarm, "High Alarm", MenuItemKind::Value, MenuScreenId::Control},
  {MenuItemId::ControlStopRun, "Stop Run", MenuItemKind::Action, MenuScreenId::Control},
  {MenuItemId::ControlBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kAlarmItems[kAlarmItemCount] = {
  {MenuItemId::AlarmAll, "All", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmSensorFault, "Sensor Fault", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmOverTemp, "Over Temp", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmHeatingIneffective, "No Temp Rise", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmMqttOffline, "MQTT Offline", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmLowProcessTemp, "Low Temp Alert", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmHighProcessTemp, "High Temp Alert", MenuItemKind::Value, MenuScreenId::Alarm},
  {MenuItemId::AlarmLogMenu, "Alarm Log", MenuItemKind::Submenu, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kAlarmLogItems[kAlarmLogItemCount] = {
  {MenuItemId::AlarmLogClearAll, "Clear All", MenuItemKind::Action, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogBack, "Back", MenuItemKind::Back, MenuScreenId::Alarm},
  {MenuItemId::AlarmLogEntry0, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry1, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry2, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry3, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry4, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry5, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry6, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog},
  {MenuItemId::AlarmLogEntry7, "Alarm Entry", MenuItemKind::ReadOnly, MenuScreenId::AlarmLog}
};

const MenuItemDefinition kPidItems[kPidItemCount] = {
  {MenuItemId::PidKp, "Kp", MenuItemKind::Value, MenuScreenId::PidSettings},
  {MenuItemId::PidKi, "Ki", MenuItemKind::Value, MenuScreenId::PidSettings},
  {MenuItemId::PidKd, "Kd", MenuItemKind::Value, MenuScreenId::PidSettings},
  {MenuItemId::PidDirection, "Direction", MenuItemKind::Value, MenuScreenId::PidSettings},
  {MenuItemId::PidOutputLimit, "Output Limit", MenuItemKind::Value, MenuScreenId::PidSettings},
  {MenuItemId::PidAutotune, "Autotune", MenuItemKind::Action, MenuScreenId::PidSettings},
  {MenuItemId::PidBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kNetworkItems[kNetworkItemCount] = {
  {MenuItemId::NetworkWifiStatus, "Wi-Fi Status", MenuItemKind::ReadOnly, MenuScreenId::Network},
  {MenuItemId::NetworkIpAddress, "IP Address", MenuItemKind::ReadOnly, MenuScreenId::Network},
  {MenuItemId::NetworkBrokerHost, "Broker Host", MenuItemKind::ReadOnly, MenuScreenId::Network},
  {MenuItemId::NetworkBrokerPort, "Broker Port", MenuItemKind::ReadOnly, MenuScreenId::Network},
  {MenuItemId::NetworkClearWifi, "Clear Wi-Fi", MenuItemKind::Action, MenuScreenId::Network},
  {MenuItemId::NetworkBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kIntegrationItems[kIntegrationItemCount] = {
  {MenuItemId::IntegrationSystemName, "Paired System", MenuItemKind::ReadOnly, MenuScreenId::Integration},
  {MenuItemId::IntegrationControllerId, "Controller ID", MenuItemKind::ReadOnly, MenuScreenId::Integration},
  {MenuItemId::IntegrationControllerLink, "Controller Link", MenuItemKind::ReadOnly, MenuScreenId::Integration},
  {MenuItemId::IntegrationUnpair, "Unpair", MenuItemKind::Action, MenuScreenId::Integration},
  {MenuItemId::IntegrationBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuItemDefinition kDeviceItems[kDeviceItemCount] = {
  {MenuItemId::DeviceBrightness, "Brightness", MenuItemKind::Value, MenuScreenId::Device},
  {MenuItemId::DeviceBuzzer, "Buzzer", MenuItemKind::Value, MenuScreenId::Device},
  {MenuItemId::DeviceCalibrationOffset, "Calibration Offset", MenuItemKind::Value, MenuScreenId::Device},
  {MenuItemId::DeviceDeviceId, "Device ID", MenuItemKind::ReadOnly, MenuScreenId::Device},
  {MenuItemId::DeviceFirmware, "Firmware", MenuItemKind::ReadOnly, MenuScreenId::Device},
  {MenuItemId::DeviceResetWifi, "Reset Wi-Fi", MenuItemKind::Action, MenuScreenId::Device},
  {MenuItemId::DeviceBack, "Back", MenuItemKind::Back, MenuScreenId::Root}
};

const MenuScreenDefinition kScreens[] = {
  {MenuScreenId::Root, "Menu", kRootItems, kRootItemCount},
  {MenuScreenId::Status, "Status", kStatusItems, kStatusItemCount},
  {MenuScreenId::Control, "Control", kControlItems, kControlItemCount},
  {MenuScreenId::Alarm, "Alarm", kAlarmItems, kAlarmItemCount},
  {MenuScreenId::AlarmLog, "Alarm Log", kAlarmLogItems, kAlarmLogItemCount},
  {MenuScreenId::PidSettings, "PID Settings", kPidItems, kPidItemCount},
  {MenuScreenId::Network, "Network", kNetworkItems, kNetworkItemCount},
  {MenuScreenId::Integration, "Integration", kIntegrationItems, kIntegrationItemCount},
  {MenuScreenId::Device, "Device", kDeviceItems, kDeviceItemCount}
};
}

void MenuSystem::reset() {
  _stack[0] = MenuScreenId::Root;
  _selectedIndex[0] = 0;
  _depth = 1;
  _editing = false;
}

bool MenuSystem::navigate(int32_t diff) {
  if (diff == 0) return false;
  const MenuScreenDefinition& screen = currentScreen();
  if (screen.itemCount == 0) return false;

  const int current = static_cast<int>(_selectedIndex[currentDepthIndex()]);
  const int step = (diff > 0) ? 1 : -1;
  const int next = constrain(current + step, 0, static_cast<int>(screen.itemCount) - 1);
  if (next == current) return false;
  _selectedIndex[currentDepthIndex()] = static_cast<uint8_t>(next);
  return true;
}

bool MenuSystem::back() {
  _editing = false;
  if (_depth <= 1) return false;
  --_depth;
  return true;
}

bool MenuSystem::enter(MenuScreenId screen) {
  if (_depth >= 8) return false;
  _stack[_depth] = screen;
  _selectedIndex[_depth] = 0;
  ++_depth;
  _editing = false;
  return true;
}

void MenuSystem::setEditing(bool editing) {
  _editing = editing;
}

MenuScreenId MenuSystem::currentScreenId() const {
  return _stack[currentDepthIndex()];
}

const MenuScreenDefinition& MenuSystem::currentScreen() const {
  return definitionFor(currentScreenId());
}

const MenuItemDefinition& MenuSystem::currentItem() const {
  const MenuScreenDefinition& screen = currentScreen();
  return screen.items[_selectedIndex[currentDepthIndex()]];
}

uint8_t MenuSystem::selectedIndex() const {
  return _selectedIndex[currentDepthIndex()];
}

void MenuSystem::buildRenderState(MenuRenderState& out, MenuValueFormatter formatter, void* context) const {
  memset(&out, 0, sizeof(out));
  const MenuScreenDefinition& screen = currentScreen();
  strlcpy(out.title, screen.title, sizeof(out.title));
  out.editing = _editing;
  out.itemCount = screen.itemCount;
  out.selectedIndex = selectedIndex();

  for (uint8_t i = 0; i < screen.itemCount && i < 10; ++i) {
    const MenuItemDefinition& item = screen.items[i];
    out.items[i].id = item.id;
    out.items[i].kind = item.kind;
    out.items[i].selected = (i == out.selectedIndex);
    strlcpy(out.items[i].label, item.label, sizeof(out.items[i].label));
    if (formatter) formatter(item.id, out.items[i], context);
  }
}

const MenuScreenDefinition& MenuSystem::definitionFor(MenuScreenId id) {
  for (const auto& screen : kScreens) {
    if (screen.id == id) return screen;
  }
  return kScreens[0];
}

uint8_t MenuSystem::currentDepthIndex() const {
  return _depth == 0 ? 0 : static_cast<uint8_t>(_depth - 1);
}
