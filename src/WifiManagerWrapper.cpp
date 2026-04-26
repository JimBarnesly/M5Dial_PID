#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>
#include <esp_wifi_types.h>

namespace {
constexpr uint32_t kReconnectIntervalMs = 10000;

const char* wifiDisconnectReasonText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED: return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE: return "auth_expire";
    case WIFI_REASON_AUTH_LEAVE: return "auth_leave";
    case WIFI_REASON_ASSOC_EXPIRE: return "assoc_expire";
    case WIFI_REASON_ASSOC_TOOMANY: return "assoc_toomany";
    case WIFI_REASON_NOT_AUTHED: return "not_authed";
    case WIFI_REASON_NOT_ASSOCED: return "not_assoced";
    case WIFI_REASON_ASSOC_LEAVE: return "assoc_leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "assoc_not_authed";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "disassoc_pwrcap_bad";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "disassoc_supchan_bad";
    case WIFI_REASON_IE_INVALID: return "ie_invalid";
    case WIFI_REASON_MIC_FAILURE: return "mic_failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4way_timeout";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "group_key_timeout";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "ie_4way_differs";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "group_cipher_invalid";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "pairwise_cipher_invalid";
    case WIFI_REASON_AKMP_INVALID: return "akmp_invalid";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "unsupp_rsn_ie_version";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "invalid_rsn_ie_cap";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "8021x_auth_failed";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "cipher_rejected";
    case WIFI_REASON_BEACON_TIMEOUT: return "beacon_timeout";
    case WIFI_REASON_NO_AP_FOUND: return "no_ap_found";
    case WIFI_REASON_AUTH_FAIL: return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL: return "assoc_fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake_timeout";
    case WIFI_REASON_CONNECTION_FAIL: return "connection_fail";
    default: return "unknown";
  }
}
}

void WifiManagerWrapper::transitionTo(State next) {
  if (_state == next) return;
  _state = next;

  const char* label = "idle";
  switch (_state) {
    case State::Idle: label = "idle"; break;
    case State::Connecting: label = "connecting"; break;
    case State::Connected: label = "connected"; break;
    case State::RetryBackoff: label = "retry_backoff"; break;
    case State::Unavailable: label = "unavailable"; break;
  }
  Serial.printf("[WiFi] state=%s integrated=%d\n", label, _integratedMode);
}

void WifiManagerWrapper::ensureEventLogging() {
  if (_eventLoggingInstalled) return;

  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.printf("[WiFi] sta_connected ssid=%s integrated=%d\n",
                      WiFi.SSID().c_str(),
                      _integratedMode);
        break;

      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[WiFi] got_ip ip=%s gateway=%s rssi=%d ssid=%s integrated=%d\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.RSSI(),
                      WiFi.SSID().c_str(),
                      _integratedMode);
        transitionTo(State::Connected);
        break;

      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        Serial.printf("[WiFi] disconnected reason=%u (%s) ssid=%s integrated=%d\n",
                      reason,
                      wifiDisconnectReasonText(reason),
                      hasManagedCredentials() ? _managedSsid : WiFi.SSID().c_str(),
                      _integratedMode);
        if (hasManagedCredentials()) transitionTo(State::RetryBackoff);
        else transitionTo(State::Unavailable);
        break;
      }

      default:
        break;
    }
  });

  _eventLoggingInstalled = true;
}

bool WifiManagerWrapper::hasManagedCredentials() const {
  return _managedSsid[0] != '\0';
}

void WifiManagerWrapper::restartStaAndConnect(const char* ssid, const char* password) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);

  WiFi.disconnect(true, true);
  delay(500);

  WiFi.mode(WIFI_OFF);
  delay(500);

  WiFi.mode(WIFI_STA);
  delay(250);

  _lastReconnectAttemptMs = millis();
  Serial.printf("[WiFi] begin ssid=%s integrated=%d\n", ssid ? ssid : "", _integratedMode);
  WiFi.begin(ssid, password);
}

void WifiManagerWrapper::beginStandalone() {
  ensureEventLogging();
  _integratedMode = false;
  _managedSsid[0] = '\0';
  _managedPass[0] = '\0';
  _lastReconnectAttemptMs = 0;
  _connectAllowedAtMs = 0;
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(120);
  WiFi.mode(WIFI_OFF);
  transitionTo(State::Unavailable);
  Serial.println("[WiFi] no explicit network credentials; remaining local-only");
  _started = true;
}

void WifiManagerWrapper::beginIntegrated(const char* ssid, const char* password) {
  ensureEventLogging();
  _integratedMode = true;
  strlcpy(_managedSsid, ssid ? ssid : "", sizeof(_managedSsid));
  strlcpy(_managedPass, password ? password : "", sizeof(_managedPass));
  _lastReconnectAttemptMs = 0;
  _connectAllowedAtMs = millis() + CoreConfig::WIFI_BOOT_CONNECT_DELAY_MS;

  if (!hasManagedCredentials()) {
    transitionTo(State::Unavailable);
    Serial.println("[WiFi] integrated network requested without credentials");
  } else {
    transitionTo(State::RetryBackoff);
    Serial.printf("[WiFi] delaying initial connect ssid=%s integrated=%d delay_ms=%lu\n",
                  _managedSsid,
                  _integratedMode,
                  static_cast<unsigned long>(CoreConfig::WIFI_BOOT_CONNECT_DELAY_MS));
  }
  _started = true;
}

void WifiManagerWrapper::update() {
  if (!_started) return;

  if (WiFi.status() == WL_CONNECTED) {
    transitionTo(State::Connected);
    return;
  }

  if (!hasManagedCredentials()) {
    transitionTo(State::Unavailable);
    return;
  }

  if (_state == State::Connected) transitionTo(State::RetryBackoff);
  if (_state == State::Idle) transitionTo(State::RetryBackoff);

  const uint32_t now = millis();
  if (_lastReconnectAttemptMs == 0 && now < _connectAllowedAtMs) return;

  if (_lastReconnectAttemptMs == 0 || now - _lastReconnectAttemptMs >= kReconnectIntervalMs) {
    Serial.printf("[WiFi] reconnect attempt ssid=%s integrated=%d\n", _managedSsid, _integratedMode);
    restartStaAndConnect(_managedSsid, _managedPass);
    transitionTo(State::Connecting);
  }
}

bool WifiManagerWrapper::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void WifiManagerWrapper::resetSettings() {
  _managedSsid[0] = '\0';
  _managedPass[0] = '\0';
  _integratedMode = false;
  _lastReconnectAttemptMs = 0;
  _connectAllowedAtMs = 0;
  WiFi.disconnect(true, true);
  delay(120);
  WiFi.mode(WIFI_OFF);
  delay(120);
  transitionTo(State::Unavailable);
  Serial.println("[WiFi] explicit network credentials cleared");
}
