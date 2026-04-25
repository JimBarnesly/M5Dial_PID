#include "WifiManagerWrapper.h"
#include "core/CoreConfig.h"
#include <WiFi.h>
#include <esp_wifi_types.h>

namespace {
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
    case State::PortalActive: label = "portal_active"; break;
    case State::Connected: label = "connected"; break;
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
                      _integratedMode ? _managedSsid : WiFi.SSID().c_str(),
                      _integratedMode);

        if (!_integratedMode && reason == WIFI_REASON_AUTH_EXPIRE) {
          const uint32_t now = millis();
          if (_lastAuthExpireMs == 0 || now - _lastAuthExpireMs > 15000) _authExpireCount = 1;
          else if (_authExpireCount < 255) ++_authExpireCount;
          _lastAuthExpireMs = now;

          if (_authExpireCount >= 3) _portalForced = false;
        }

        if (_state == State::Connected) transitionTo(State::Connecting);
        break;
      }

      default:
        break;
    }
  });

  _eventLoggingInstalled = true;
}

bool WifiManagerWrapper::hasSavedCredentials() const {
  return WiFi.SSID().length() > 0;
}

void WifiManagerWrapper::startBackgroundPortal() {
  if (_portalForced) return;

  Serial.printf("[WiFi] starting background portal ap=%s timeout=%u\n", _apName, _portalTimeoutSec);
  _wm.startConfigPortal(_apName, _apPass);
  _portalForced = true;
  transitionTo(State::PortalActive);
}

void WifiManagerWrapper::buildPortalCredentials() {
  uint64_t efuseMac = ESP.getEfuseMac();
  uint32_t suffix = static_cast<uint32_t>(efuseMac & 0xFFFFFFULL);
  snprintf(_apName, sizeof(_apName), "%s%06lX", CoreConfig::WIFI_AP_NAME_PREFIX, static_cast<unsigned long>(suffix));
  snprintf(_apPass,
           sizeof(_apPass),
           "%s%06lX!",
           CoreConfig::WIFI_AP_PASS_PREFIX,
           static_cast<unsigned long>(suffix));
}

void WifiManagerWrapper::beginStandalone(uint16_t portalTimeoutSec) {
  buildPortalCredentials();
  ensureEventLogging();
  _integratedMode = false;
  _managedSsid[0] = '\0';
  _managedPass[0] = '\0';
  _portalTimeoutSec = portalTimeoutSec;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.persistent(true);

  _wm.setDebugOutput(true);
  _wm.setConnectRetries(8);
  _wm.setConfigPortalBlocking(false);
  _wm.setConfigPortalTimeout(portalTimeoutSec);

  _portalForced = false;
  _authExpireCount = 0;
  _lastAuthExpireMs = 0;
  _lastReconnectAttemptMs = millis();
  transitionTo(State::Idle);

  if (hasSavedCredentials()) {
    Serial.printf("[WiFi] attempting saved credentials SSID=%s\n", WiFi.SSID().c_str());
    WiFi.begin();
    transitionTo(State::Connecting);
  } else {
    Serial.println("[WiFi] no saved credentials; portal deferred to background update");
  }

  _started = true;
}

void WifiManagerWrapper::beginIntegrated(const char* ssid, const char* password) {
  buildPortalCredentials();
  ensureEventLogging();
  _integratedMode = true;
  strlcpy(_managedSsid, ssid ? ssid : "", sizeof(_managedSsid));
  strlcpy(_managedPass, password ? password : "", sizeof(_managedPass));

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.persistent(false);

  _portalForced = false;
  _authExpireCount = 0;
  _lastAuthExpireMs = 0;
  _lastReconnectAttemptMs = millis();
  transitionTo(State::Connecting);

  Serial.printf("[WiFi] integrated connect SSID=%s\n", _managedSsid);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(_managedSsid, _managedPass);
  _started = true;
}

void WifiManagerWrapper::update() {
  if (!_started) return;

  if (WiFi.status() == WL_CONNECTED) {
    transitionTo(State::Connected);
  } else if (_state == State::Connected) {
    transitionTo(State::Connecting);
  }

  if (_integratedMode) {
    if (WiFi.status() != WL_CONNECTED && millis() - _lastReconnectAttemptMs > 10000) {
      _lastReconnectAttemptMs = millis();
      Serial.printf("[WiFi] reconnect attempt integrated SSID=%s\n", _managedSsid);
      WiFi.disconnect(false, false);
      delay(50);
      WiFi.begin(_managedSsid, _managedPass);
      transitionTo(State::Connecting);
    }
    return;
  }

  _wm.process();
  if (WiFi.status() == WL_CONNECTED) return;

  if (_authExpireCount >= 6 && !_portalForced) {
    Serial.println("[WiFi] repeated AUTH_EXPIRE; clearing settings and starting portal");
    resetSettings();
    _authExpireCount = 0;
    startBackgroundPortal();
    return;
  }

  if (!_portalForced) {
    if (hasSavedCredentials()) {
      if (millis() - _lastReconnectAttemptMs > 10000) {
        _lastReconnectAttemptMs = millis();
        Serial.println("[WiFi] reconnect attempt using saved credentials");
        WiFi.disconnect(false, false);
        delay(50);
        WiFi.begin();
        transitionTo(State::Connecting);
      }
    } else {
      startBackgroundPortal();
    }
  }
}

bool WifiManagerWrapper::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

const char* WifiManagerWrapper::getPortalApName() const {
  return _apName;
}

const char* WifiManagerWrapper::getPortalApPassword() const {
  return _apPass;
}

void WifiManagerWrapper::resetSettings() {
  _wm.resetSettings();
  _managedSsid[0] = '\0';
  _managedPass[0] = '\0';
  _integratedMode = false;
  _portalForced = false;
  WiFi.disconnect(true, true);
  delay(120);
  WiFi.mode(WIFI_OFF);
  delay(120);
  WiFi.mode(WIFI_STA);
  transitionTo(State::Idle);
}
