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
  bool isConnected();

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
  char _clientId[40] {};
  CommandCallback _commandCallback;

  void buildClientId();
  void configureClientForSecurity(bool force = false);
  uint16_t effectivePort() const;
  void tryReconnect();
  void handleMessage(char* topic, byte* payload, unsigned int length);
  void subscribeTopics();
  void publishShadow(const RuntimeState& rt, uint32_t remainingSec);
};
