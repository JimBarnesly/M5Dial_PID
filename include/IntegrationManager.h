#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "AppState.h"
#include "MqttManager.h"
#include "StorageManager.h"
#include "WifiManagerWrapper.h"

struct CommissioningPayload {
  uint16_t version {1};
  char systemId[24] {""};
  char systemName[32] {""};
  char controllerId[24] {""};
  char controllerPublicKey[192] {""};
  char apSsid[32] {""};
  char apPsk[64] {""};
  char brokerHost[64] {""};
  uint16_t brokerPort {0};
  uint32_t issuedAt {0};
  uint32_t epoch {0};
  char signature[128] {""};
};

class BootstrapVerifier {
public:
  virtual ~BootstrapVerifier() = default;
  virtual bool verify(const CommissioningPayload& payload, char* reason, size_t reasonSize) = 0;
};

class DevelopmentBootstrapVerifier final : public BootstrapVerifier {
public:
  bool verify(const CommissioningPayload& payload, char* reason, size_t reasonSize) override;
};

class ProductionBootstrapVerifierStub final : public BootstrapVerifier {
public:
  bool verify(const CommissioningPayload& payload, char* reason, size_t reasonSize) override;
};

class DevelopmentCommissioningCardReader {
public:
  bool injectJson(const char* json, char* reason, size_t reasonSize);
  bool poll(CommissioningPayload& payload);

private:
  bool _pending {false};
  CommissioningPayload _payload;
};

class IntegrationManager {
public:
  void begin(IntegrationBinding* binding,
             PersistentConfig* cfg,
             RuntimeState* rt,
             StorageManager* storage,
             WifiManagerWrapper* wifi,
             MqttManager* mqtt);
  void update();
  bool handleMqttMessage(const char* topic, const char* payload);
  void startPairingWindow(uint32_t durationMs = CoreConfig::PAIRING_WINDOW_MS);
  bool injectDevelopmentCard(const char* json, char* reason, size_t reasonSize);
  void unpairToStandalone(bool clearWifiSettings);
  bool shouldBootIntegratedNetworking() const;
  const char* bootSsid() const;
  const char* bootPassword() const;
  const char* bootBrokerHost() const;
  uint16_t bootBrokerPort() const;
  const IntegrationBinding& binding() const;

private:
  IntegrationBinding* _binding {nullptr};
  PersistentConfig* _cfg {nullptr};
  RuntimeState* _rt {nullptr};
  StorageManager* _storage {nullptr};
  WifiManagerWrapper* _wifi {nullptr};
  MqttManager* _mqtt {nullptr};
  DevelopmentCommissioningCardReader _devReader;
  DevelopmentBootstrapVerifier _devVerifier;
  ProductionBootstrapVerifierStub _productionVerifier;
  uint32_t _lastEnrollmentAttemptMs {0};

  void syncRuntimeFromBinding();
  bool hasBootstrapMetadata() const;
  void applyBootstrapPayload(const CommissioningPayload& payload);
  void sendEnrollmentRequest();
  void markEnrollmentSuccess(const JsonDocument& doc);
  void applyControllerDisconnectFallback();
  void clearControllerDisconnectFallback();
  void noteControllerSupervision();
  static const char* operatingModeText(OperatingMode mode);
  static const char* deviceTypeText(DeviceType type);
  static void deriveControllerFingerprint(const char* key, char* out, size_t outSize);
};
