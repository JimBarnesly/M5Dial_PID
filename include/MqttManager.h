#pragma once
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "AppState.h"

class MqttManager {
public:
  MqttManager();
  void begin(PersistentConfig* cfg, RuntimeState* rt);
  void update();
  void publishStatus(const RuntimeState& rt, const char* activeStageName, uint32_t remainingSec);
  void publishProfileCompleteIfPending(RuntimeState& rt);
  void publishConfig(const PersistentConfig& cfg, const RuntimeState& rt);
  bool isConnected();

  using CommandCallback = std::function<void(const char* topic, const char* payload)>;
  void setCommandCallback(CommandCallback cb);

private:
  WiFiClient _wifiClient;
  PubSubClient _client;
  PersistentConfig* _cfg {nullptr};
  RuntimeState* _rt {nullptr};
  uint32_t _lastReconnectMs {0};
  CommandCallback _commandCallback;

  void tryReconnect();
  void handleMessage(char* topic, byte* payload, unsigned int length);
  void subscribeTopics();
};
