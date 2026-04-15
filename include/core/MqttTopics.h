#pragma once

namespace MqttTopics {
namespace Topic {
constexpr char CmdWildcard[] = "/cmd/#";
constexpr char Status[] = "/status";
constexpr char Shadow[] = "/shadow";
constexpr char EventCmdAck[] = "/event/cmd_ack";
constexpr char StatusCalibration[] = "/status/calibration";
constexpr char EventProfileComplete[] = "/event/profile_complete";
constexpr char ConfigEffective[] = "/config/effective";
constexpr char EventLog[] = "/event/log";
}  // namespace Topic

namespace Cmd {
constexpr char Setpoint[] = "/cmd/setpoint";
constexpr char OverTemp[] = "/cmd/over_temp";
constexpr char ControlLock[] = "/cmd/control_lock";
constexpr char MqttPort[] = "/cmd/mqtt_port";
constexpr char MqttTls[] = "/cmd/mqtt_tls";
constexpr char MqttTimeout[] = "/cmd/mqtt_timeout";
constexpr char MqttFallback[] = "/cmd/mqtt_fallback";
constexpr char WifiPortalTimeout[] = "/cmd/wifi_portal_timeout";
constexpr char ResetWifi[] = "/cmd/reset_wifi";
constexpr char Pid[] = "/cmd/pid";
constexpr char PidKp[] = "/cmd/pid_kp";
constexpr char PidKi[] = "/cmd/pid_ki";
constexpr char PidKd[] = "/cmd/pid_kd";
constexpr char GetConfig[] = "/cmd/get_config";
constexpr char GetEvents[] = "/cmd/get_events";
constexpr char ProfileSelect[] = "/cmd/profile_select";
constexpr char ProfileStart[] = "/cmd/profile_start";
constexpr char ProfileDelete[] = "/cmd/profile_delete";
constexpr char ProfileUpsert[] = "/cmd/profile_upsert";
constexpr char Minutes[] = "/cmd/minutes";
constexpr char Start[] = "/cmd/start";
constexpr char Pause[] = "/cmd/pause";
constexpr char Stop[] = "/cmd/stop";
constexpr char ResetAlarm[] = "/cmd/reset_alarm";
constexpr char AckAlarm[] = "/cmd/ack_alarm";
constexpr char StartAutotune[] = "/cmd/start_autotune";
constexpr char AcceptTune[] = "/cmd/accept_tune";
constexpr char RejectTune[] = "/cmd/reject_tune";
constexpr char TempCalibration[] = "/cmd/temp_calibration";
constexpr char CalibrationStatus[] = "/cmd/calibration_status";
}  // namespace Cmd
}  // namespace MqttTopics
