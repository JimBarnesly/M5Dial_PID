#include "IntegrationManager.h"

#include <ArduinoJson.h>
#include <cstring>

#include "DebugControl.h"
#include "core/CoreConfig.h"

namespace {
constexpr char kDevBootstrapSignature[] = "dev-allow";
constexpr char kEnrollmentRequestLeaf[] = "/enrollment/request";
constexpr char kEnrollmentResponseLeaf[] = "/enrollment/response";
constexpr char kControllerHeartbeatLeaf[] = "/controller/heartbeat";
constexpr char kPairingModeLeaf[] = "/cmd/pairing_mode";
constexpr char kBootstrapInjectLeaf[] = "/cmd/bootstrap_inject";
constexpr char kUnpairLeaf[] = "/cmd/unpair";

bool loadPayloadFromJson(const char* json, CommissioningPayload& payload, char* reason, size_t reasonSize) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json ? json : "");
  if (err) {
    strlcpy(reason, "invalid_json", reasonSize);
    return false;
  }

  payload = CommissioningPayload();
  payload.version = doc["version"] | 1;
  strlcpy(payload.systemId, doc["system_id"] | "", sizeof(payload.systemId));
  strlcpy(payload.systemName, doc["system_name"] | "", sizeof(payload.systemName));
  strlcpy(payload.controllerId, doc["controller_id"] | "", sizeof(payload.controllerId));
  strlcpy(payload.controllerPublicKey, doc["controller_public_key"] | "", sizeof(payload.controllerPublicKey));
  strlcpy(payload.apSsid, doc["ap_ssid"] | "", sizeof(payload.apSsid));
  strlcpy(payload.apPsk, doc["ap_psk"] | "", sizeof(payload.apPsk));
  strlcpy(payload.brokerHost, doc["broker_host"] | "", sizeof(payload.brokerHost));
  payload.brokerPort = doc["broker_port"] | 0;
  payload.issuedAt = doc["issued_at"] | 0;
  payload.epoch = doc["epoch"] | 0;
  strlcpy(payload.signature, doc["signature"] | "", sizeof(payload.signature));

  if (payload.systemId[0] == '\0' ||
      payload.controllerId[0] == '\0' ||
      payload.controllerPublicKey[0] == '\0' ||
      payload.apSsid[0] == '\0' ||
      payload.brokerHost[0] == '\0' ||
      payload.brokerPort == 0) {
    strlcpy(reason, "missing_required_fields", reasonSize);
    return false;
  }

  strlcpy(reason, "ok", reasonSize);
  return true;
}
}

bool DevelopmentBootstrapVerifier::verify(const CommissioningPayload& payload, char* reason, size_t reasonSize) {
  if (payload.signature[0] == '\0') {
    strlcpy(reason, "missing_signature", reasonSize);
    return false;
  }
  if (strcmp(payload.signature, kDevBootstrapSignature) != 0) {
    strlcpy(reason, "dev_signature_rejected", reasonSize);
    return false;
  }
  strlcpy(reason, "dev_signature_accepted", reasonSize);
  return true;
}

bool ProductionBootstrapVerifierStub::verify(const CommissioningPayload&, char* reason, size_t reasonSize) {
  strlcpy(reason, "production_verifier_not_implemented", reasonSize);
  return false;
}

bool DevelopmentCommissioningCardReader::injectJson(const char* json, char* reason, size_t reasonSize) {
  CommissioningPayload payload;
  if (!loadPayloadFromJson(json, payload, reason, reasonSize)) return false;
  _payload = payload;
  _pending = true;
  return true;
}

bool DevelopmentCommissioningCardReader::poll(CommissioningPayload& payload) {
  if (!_pending) return false;
  payload = _payload;
  _pending = false;
  return true;
}

void IntegrationManager::begin(IntegrationBinding* binding,
                               PersistentConfig* cfg,
                               RuntimeState* rt,
                               StorageManager* storage,
                               WifiManagerWrapper* wifi,
                               MqttManager* mqtt) {
  _binding = binding;
  _cfg = cfg;
  _rt = rt;
  _storage = storage;
  _wifi = wifi;
  _mqtt = mqtt;
  syncRuntimeFromBinding();
}

void IntegrationManager::syncRuntimeFromBinding() {
  if (!_binding || !_rt || !_cfg || !_mqtt) return;

  _rt->pairedMetadataPresent = hasBootstrapMetadata();
  _rt->operatingMode = _binding->operatingMode;
  _rt->integrationState = _binding->integrationState;
  _rt->controllerEnrollmentPending = (_binding->integrationState == IntegrationState::BootstrapPending);
  _rt->deviceType = _binding->deviceType;
  strlcpy(_rt->systemId, _rt->pairedMetadataPresent ? _binding->systemId : _cfg->systemId, sizeof(_rt->systemId));
  strlcpy(_rt->systemName, _binding->systemName, sizeof(_rt->systemName));
  strlcpy(_rt->controllerId, _binding->controllerId, sizeof(_rt->controllerId));

  if (_rt->pairedMetadataPresent && _binding->brokerHost[0] != '\0') {
    _mqtt->setBrokerOverride(_binding->brokerHost, _binding->brokerPort);
  } else {
    _mqtt->clearBrokerOverride();
  }
}

bool IntegrationManager::hasBootstrapMetadata() const {
  return _binding &&
         (_binding->integrationState != IntegrationState::None ||
          _binding->paired ||
          _binding->systemId[0] != '\0' && strcmp(_binding->systemId, CoreConfig::MQTT_DEFAULT_SYSTEM_ID) != 0);
}

void IntegrationManager::startPairingWindow(uint32_t durationMs) {
  if (!_rt) return;
  _rt->pairingWindowActive = true;
  _rt->pairingWindowEndsAtMs = millis() + durationMs;
  Serial.printf("[INTEGRATION] pairing window opened duration_ms=%lu\n", static_cast<unsigned long>(durationMs));
}

bool IntegrationManager::injectDevelopmentCard(const char* json, char* reason, size_t reasonSize) {
  return _devReader.injectJson(json, reason, reasonSize);
}

void IntegrationManager::applyBootstrapPayload(const CommissioningPayload& payload) {
  if (!_binding || !_storage || !_rt) return;

  _binding->schemaVersion = payload.version;
  _binding->operatingMode = OperatingMode::Standalone;
  _binding->integrationState = IntegrationState::BootstrapPending;
  _binding->paired = false;
  strlcpy(_binding->systemId, payload.systemId, sizeof(_binding->systemId));
  strlcpy(_binding->systemName, payload.systemName, sizeof(_binding->systemName));
  strlcpy(_binding->controllerId, payload.controllerId, sizeof(_binding->controllerId));
  strlcpy(_binding->controllerPublicKey, payload.controllerPublicKey, sizeof(_binding->controllerPublicKey));
  strlcpy(_binding->apSsid, payload.apSsid, sizeof(_binding->apSsid));
  strlcpy(_binding->apPsk, payload.apPsk, sizeof(_binding->apPsk));
  strlcpy(_binding->brokerHost, payload.brokerHost, sizeof(_binding->brokerHost));
  _binding->brokerPort = payload.brokerPort;
  _binding->issuedAt = payload.issuedAt;
  _binding->epoch = payload.epoch;
  deriveControllerFingerprint(_binding->controllerPublicKey,
                              _binding->controllerFingerprint,
                              sizeof(_binding->controllerFingerprint));
  _storage->saveIntegrationBinding(*_binding);
  syncRuntimeFromBinding();

  _rt->pairingWindowActive = false;
  _rt->controllerConnected = false;
  _rt->lastControllerSupervisionAtMs = 0;
  Serial.printf("[INTEGRATION] bootstrap accepted system_id=%s controller_id=%s mode=%s state=%u\n",
                _binding->systemId,
                _binding->controllerId,
                operatingModeText(_binding->operatingMode),
                static_cast<unsigned>(_binding->integrationState));
}

void IntegrationManager::sendEnrollmentRequest() {
  if (!_binding || !_rt || !_mqtt || !_mqtt->isConnected()) return;
  if (_binding->integrationState != IntegrationState::BootstrapPending) return;
  if (millis() - _lastEnrollmentAttemptMs < CoreConfig::CONTROLLER_ENROLL_RETRY_MS) return;
  _lastEnrollmentAttemptMs = millis();

  JsonDocument doc;
  doc["type"] = "enrollment_request";
  doc["system_id"] = _binding->systemId;
  doc["controller_id"] = _binding->controllerId;
  doc["device_id"] = _cfg ? _cfg->deviceId : "";
  doc["device_type"] = deviceTypeText(_binding->deviceType);
  doc["firmware_version"] = CoreConfig::FIRMWARE_VERSION;
  doc["controller_fingerprint"] = _binding->controllerFingerprint;
  doc["epoch"] = _binding->epoch;
  doc["issued_at"] = _binding->issuedAt;

  String out;
  serializeJson(doc, out);
  if (_mqtt->publishRaw(kEnrollmentRequestLeaf, out.c_str(), false)) {
    Serial.printf("[INTEGRATION] enrollment request sent system_id=%s controller_id=%s\n",
                  _binding->systemId,
                  _binding->controllerId);
  } else {
    Serial.println("[INTEGRATION] enrollment request publish failed");
  }
}

void IntegrationManager::markEnrollmentSuccess(const JsonDocument& doc) {
  if (!_binding || !_storage || !_rt) return;

  const char* responseFingerprint = doc["controller_fingerprint"] | _binding->controllerFingerprint;
  if (responseFingerprint[0] != '\0') {
    strlcpy(_binding->controllerFingerprint, responseFingerprint, sizeof(_binding->controllerFingerprint));
  }

  _binding->paired = true;
  _binding->integrationState = IntegrationState::Enrolled;
  _binding->operatingMode = OperatingMode::Integrated;
  _storage->saveIntegrationBinding(*_binding);
  syncRuntimeFromBinding();
  noteControllerSupervision();
  _rt->controllerEnrollmentPending = false;
  Serial.printf("[INTEGRATION] enrollment success system_id=%s controller_id=%s mode=%s\n",
                _binding->systemId,
                _binding->controllerId,
                operatingModeText(_binding->operatingMode));
}

void IntegrationManager::noteControllerSupervision() {
  if (!_rt) return;
  _rt->controllerConnected = true;
  _rt->lastControllerSupervisionAtMs = millis();
  clearControllerDisconnectFallback();
}

void IntegrationManager::applyControllerDisconnectFallback() {
  if (!_rt) return;
  if (_rt->integratedFallbackActive) return;

  _rt->integratedFallbackActive = true;
  _rt->controllerConnected = false;
  switch (_rt->deviceType) {
    case DeviceType::ThermalController:
      // Thermal controllers keep local real-time control active and hold their
      // current safe runtime state when controller supervision is lost.
      Serial.println("[INTEGRATION] controller supervision lost; thermal controller holding current safe local control state");
      break;
    case DeviceType::PumpController:
      // Pump controllers fail safe to off when controller supervision is lost.
      Serial.println("[INTEGRATION] controller supervision lost; pump controller fallback requests outputs off");
      break;
  }
}

void IntegrationManager::clearControllerDisconnectFallback() {
  if (!_rt) return;
  if (!_rt->integratedFallbackActive) return;
  _rt->integratedFallbackActive = false;
  Serial.println("[INTEGRATION] controller supervision restored; clearing integrated fallback");
}

void IntegrationManager::update() {
  if (!_rt || !_binding) return;

  if (_rt->pairingWindowActive && millis() >= _rt->pairingWindowEndsAtMs) {
    _rt->pairingWindowActive = false;
    Serial.println("[INTEGRATION] pairing window expired");
  }

  CommissioningPayload payload;
  if (_devReader.poll(payload)) {
    char reason[48] {};
    const bool ok = _devVerifier.verify(payload, reason, sizeof(reason));
    Serial.printf("[INTEGRATION] card read verification=%s reason=%s\n", ok ? "accepted" : "rejected", reason);
    if (ok) applyBootstrapPayload(payload);
  }

  if (_binding->integrationState == IntegrationState::BootstrapPending && _rt->wifiConnected && _rt->mqttConnected) {
    sendEnrollmentRequest();
  }

  if (_binding->integrationState == IntegrationState::Enrolled) {
    const bool supervisionTimedOut = (_rt->lastControllerSupervisionAtMs != 0) &&
                                     (millis() - _rt->lastControllerSupervisionAtMs >= CoreConfig::CONTROLLER_SUPERVISION_TIMEOUT_MS);
    if (!_rt->mqttConnected || supervisionTimedOut) {
      applyControllerDisconnectFallback();
    } else {
      clearControllerDisconnectFallback();
    }
  }
}

bool IntegrationManager::handleMqttMessage(const char* topic, const char* payload) {
  const String t = topic ? topic : "";
  const char* safePayload = payload ? payload : "";

  if (t.endsWith(kPairingModeLeaf)) {
    startPairingWindow();
    return true;
  }

  if (t.endsWith(kBootstrapInjectLeaf)) {
    char reason[48] {};
    const bool ok = injectDevelopmentCard(safePayload, reason, sizeof(reason));
    Serial.printf("[INTEGRATION] development bootstrap inject %s reason=%s\n", ok ? "accepted" : "rejected", reason);
    return true;
  }

  if (t.endsWith(kUnpairLeaf)) {
    unpairToStandalone(true);
    return true;
  }

  if (t.endsWith(kEnrollmentResponseLeaf)) {
    JsonDocument doc;
    if (deserializeJson(doc, safePayload)) {
      Serial.println("[INTEGRATION] enrollment response rejected invalid_json");
      return true;
    }

    const bool accepted = doc["accepted"] | false;
    const char* systemId = doc["system_id"] | "";
    const char* controllerId = doc["controller_id"] | "";
    if (!accepted ||
        !_binding ||
        strcmp(systemId, _binding->systemId) != 0 ||
        strcmp(controllerId, _binding->controllerId) != 0) {
      Serial.printf("[INTEGRATION] enrollment response rejected accepted=%d system_id=%s controller_id=%s\n",
                    accepted,
                    systemId,
                    controllerId);
      return true;
    }
    markEnrollmentSuccess(doc);
    return true;
  }

  if (t.endsWith(kControllerHeartbeatLeaf)) {
    JsonDocument doc;
    if (deserializeJson(doc, safePayload)) return true;
    const char* controllerId = doc["controller_id"] | "";
    if (_binding &&
        _binding->integrationState == IntegrationState::Enrolled &&
        strcmp(controllerId, _binding->controllerId) == 0) {
      noteControllerSupervision();
      Serial.printf("[INTEGRATION] controller heartbeat controller_id=%s\n", controllerId);
    }
    return true;
  }

  return false;
}

void IntegrationManager::unpairToStandalone(bool clearWifiSettings) {
  if (!_binding || !_storage) return;
  Serial.printf("[INTEGRATION] unpair requested clear_wifi=%d\n", clearWifiSettings);
  _storage->clearIntegrationBinding();
  *_binding = IntegrationBinding();
  syncRuntimeFromBinding();
  _rt->controllerConnected = false;
  _rt->integratedFallbackActive = false;
  _rt->pairingWindowActive = false;
  _rt->lastControllerSupervisionAtMs = 0;
  if (clearWifiSettings && _wifi) _wifi->resetSettings();
}

bool IntegrationManager::shouldBootIntegratedNetworking() const {
  return _binding &&
         _binding->apSsid[0] != '\0' &&
         (_binding->integrationState == IntegrationState::BootstrapPending ||
          _binding->integrationState == IntegrationState::Enrolled);
}

const char* IntegrationManager::bootSsid() const {
  return _binding ? _binding->apSsid : "";
}

const char* IntegrationManager::bootPassword() const {
  return _binding ? _binding->apPsk : "";
}

const char* IntegrationManager::bootBrokerHost() const {
  return (_binding && _binding->brokerHost[0] != '\0') ? _binding->brokerHost : "";
}

uint16_t IntegrationManager::bootBrokerPort() const {
  return _binding ? _binding->brokerPort : 0;
}

const IntegrationBinding& IntegrationManager::binding() const {
  return *_binding;
}

const char* IntegrationManager::operatingModeText(OperatingMode mode) {
  return mode == OperatingMode::Integrated ? "integrated" : "standalone";
}

const char* IntegrationManager::deviceTypeText(DeviceType type) {
  switch (type) {
    case DeviceType::PumpController: return "pump_controller";
    case DeviceType::ThermalController:
    default:
      return "thermal_controller";
  }
}

void IntegrationManager::deriveControllerFingerprint(const char* key, char* out, size_t outSize) {
  uint32_t hash = 2166136261u;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(key ? key : ""); *p != 0; ++p) {
    hash ^= *p;
    hash *= 16777619u;
  }
  snprintf(out, outSize, "devfnv32:%08lX", static_cast<unsigned long>(hash));
}
