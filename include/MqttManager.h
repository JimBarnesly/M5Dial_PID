#pragma once
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <functional>
#include "AppState.h"

class MqttManager {
public:
  MqttManager();
  void begin(PersistentConfig* cfg, RuntimeState* rt);
  void update();
  void publishStatus(const RuntimeState& rt, const char* activeStageName, uint32_t remainingSec);
  void publishCommandAck(const char* cmdId,
                         const char* command,
                         bool accepted,
                         bool applied,
                         const char* reason,
                         const RuntimeState& rt,
                         uint32_t remainingSec);
  void publishCalibrationStatus(const PersistentConfig& cfg, const RuntimeState& rt);
  void publishProfileCompleteIfPending(RuntimeState& rt);
  void publishConfig(const PersistentConfig& cfg, const RuntimeState& rt);
  void publishEventLog(const RuntimeState& rt);
  void publishLifecycleEvent(const char* type, const char* detail = nullptr, const char* cause = nullptr);
  bool publishRaw(const char* leaf, const char* payload, bool retained = false);
  bool isConnected();
  const char* clientId() const;
  String topicBase() const;
  void setBrokerOverride(const char* host, uint16_t port);
  void clearBrokerOverride();

  using CommandCallback = std::function<void(const char* topic, const char* payload)>;
  void setCommandCallback(CommandCallback cb);

private:
  WiFiClient _wifiClient;
  WiFiClientSecure _wifiSecureClient;
  Client* _transportClient {nullptr};
  PubSubClient _client;
  PersistentConfig* _cfg {nullptr};
  RuntimeState* _rt {nullptr};
  uint32_t _lastReconnectMs {0};
  bool _lastTlsEnabled {false};
  uint16_t _lastPort {0};
  uint16_t _brokerOverridePort {0};
  char _clientId[40] {};
  char _brokerOverrideHost[64] {};
  CommandCallback _commandCallback;

  void buildClientId();
  void configureClientForSecurity(bool force = false);
  uint16_t effectivePort() const;
  void applyBrokerEndpoint();
  void tryReconnect();
  void handleMessage(char* topic, byte* payload, unsigned int length);
  void subscribeTopics();
  const char* currentBrokerHost() const;
  bool usingLiteralBrokerIp() const;
  String topicFor(const char* leaf) const;
  void publishShadow(const RuntimeState& rt, uint32_t remainingSec);
};
