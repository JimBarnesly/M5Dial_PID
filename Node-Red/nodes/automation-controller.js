module.exports = function(RED) {
  const mqtt = require("mqtt");

  const COMMAND_DEFS = [
    { type: "automation-setpoint", label: "Setpoint", leaf: "/cmd", command: "setpoint", buildPayload: buildSetpointPayload },
    { type: "automation-minutes", label: "Minutes", leaf: "/cmd", command: "minutes", buildPayload: buildMinutesPayload },
    { type: "automation-start", label: "Start", leaf: "/cmd", command: "start", buildPayload: buildSignalPayload("start") },
    { type: "automation-pause", label: "Pause", leaf: "/cmd", command: "pause", buildPayload: buildSignalPayload("pause") },
    { type: "automation-stop", label: "Stop", leaf: "/cmd", command: "stop", buildPayload: buildSignalPayload("stop") },
    { type: "automation-ack-alarm", label: "Ack Alarm", leaf: "/cmd", command: "ack_alarm", buildPayload: buildSignalPayload("ack_alarm") },
    { type: "automation-reset-alarm", label: "Reset Alarm", leaf: "/cmd", command: "reset_alarm", buildPayload: buildSignalPayload("reset_alarm") },
    { type: "automation-mqtt-host", label: "MQTT Host", leaf: "/cmd", command: "mqtt_host", buildPayload: buildMqttHostPayload },
    { type: "automation-mqtt-port", label: "MQTT Port", leaf: "/cmd", command: "mqtt_port", buildPayload: buildIntegerFieldPayload("mqtt_port", "port", 1, 65535) },
    { type: "automation-mqtt-tls", label: "MQTT TLS", leaf: "/cmd", command: "mqtt_tls", buildPayload: buildMqttTlsPayload },
    { type: "automation-mqtt-timeout", label: "MQTT Timeout", leaf: "/cmd", command: "mqtt_timeout", buildPayload: buildIntegerFieldPayload("mqtt_timeout", "seconds", 0, 3600) },
    { type: "automation-mqtt-fallback", label: "MQTT Fallback", leaf: "/cmd", command: "mqtt_fallback", buildPayload: buildEnumPayload("mqtt_fallback", "mode", [0, 1, 2]) },
    { type: "automation-reset-wifi", label: "Reset WiFi", leaf: "/cmd", command: "reset_wifi", buildPayload: buildSignalPayload("reset_wifi") },
    { type: "automation-over-temp", label: "Over Temp", leaf: "/cmd", command: "over_temp", buildPayload: buildFloatFieldPayload("over_temp", "overTempC", 20, 140) },
    { type: "automation-control-lock", label: "Control Lock", leaf: "/cmd", command: "control_lock", buildPayload: buildEnumPayload("control_lock", "controlLock", [0, 1, 2]) },
    { type: "automation-pid", label: "PID", leaf: "/cmd", command: "pid", buildPayload: buildPidPayload },
    { type: "automation-pid-kp", label: "PID Kp", leaf: "/cmd", command: "pid_kp", buildPayload: buildFloatFieldPayload("pid_kp", "kp", 0.000001, 60) },
    { type: "automation-pid-ki", label: "PID Ki", leaf: "/cmd", command: "pid_ki", buildPayload: buildFloatFieldPayload("pid_ki", "ki", 0.000001, 2) },
    { type: "automation-pid-kd", label: "PID Kd", leaf: "/cmd", command: "pid_kd", buildPayload: buildFloatFieldPayload("pid_kd", "kd", 0.000001, 80) },
    { type: "automation-start-autotune", label: "Start Autotune", leaf: "/cmd", command: "start_autotune", buildPayload: buildSignalPayload("start_autotune") },
    { type: "automation-accept-tune", label: "Accept Tune", leaf: "/cmd", command: "accept_tune", buildPayload: buildSignalPayload("accept_tune") },
    { type: "automation-reject-tune", label: "Reject Tune", leaf: "/cmd", command: "reject_tune", buildPayload: buildSignalPayload("reject_tune") },
    { type: "automation-temp-calibration", label: "Temp Calibration", leaf: "/cmd", command: "temp_calibration", buildPayload: buildTempCalibrationPayload },
    { type: "automation-calibration-status", label: "Calibration Status", leaf: "/cmd", command: "calibration_status", buildPayload: buildSignalPayload("calibration_status") },
    { type: "automation-get-config", label: "Get Config", leaf: "/cmd", command: "get_config", buildPayload: buildSignalPayload("get_config") },
    { type: "automation-get-events", label: "Get Events", leaf: "/cmd", command: "get_events", buildPayload: buildSignalPayload("get_events") },
    { type: "automation-profile-select", label: "Profile Select", leaf: "/cmd", command: "profile_select", buildPayload: buildProfileIndexPayload("profile_select") },
    { type: "automation-profile-start", label: "Profile Start", leaf: "/cmd", command: "profile_start", buildPayload: buildProfileIndexPayload("profile_start") },
    { type: "automation-profile-delete", label: "Profile Delete", leaf: "/cmd", command: "profile_delete", buildPayload: buildProfileIndexPayload("profile_delete") },
    { type: "automation-profile-upsert", label: "Profile Upsert", leaf: "/cmd", command: "profile_upsert", buildPayload: buildProfileUpsertPayload },
    { type: "automation-pairing-mode", label: "Pairing Mode", leaf: "/cmd/pairing_mode", command: null, buildPayload: buildPassthroughObjectPayload },
    { type: "automation-bootstrap-inject", label: "Bootstrap Inject", leaf: "/cmd/bootstrap_inject", command: null, buildPayload: buildObjectPayload("bootstrap payload") },
    { type: "automation-unpair", label: "Unpair", leaf: "/cmd/unpair", command: null, buildPayload: buildPassthroughObjectPayload },
    { type: "automation-enrollment-response", label: "Enrollment Response", leaf: "/enrollment/response", command: null, buildPayload: buildObjectPayload("enrollment response") },
    { type: "automation-controller-heartbeat", label: "Controller Heartbeat", leaf: "/controller/heartbeat", command: null, buildPayload: buildHeartbeatPayload }
  ];

  const RECEIVE_DEFS = [
    { type: "automation-status-in", label: "Status In", leaf: "/status" },
    { type: "automation-shadow-in", label: "Shadow In", leaf: "/shadow" },
    { type: "automation-config-in", label: "Config In", leaf: "/config", filterType: "config_effective" },
    { type: "automation-command-ack-in", label: "Command Ack In", leaf: "/config", filterType: "cmd_ack" },
    { type: "automation-events-in", label: "Events In", leaf: "/events" },
    { type: "automation-calibration-in", label: "Calibration In", leaf: "/calibration" },
    { type: "automation-lifecycle-in", label: "Lifecycle In", leaf: "/lifecycle" },
    { type: "automation-enrollment-request-in", label: "Enrollment Request In", leaf: "/enrollment/request" }
  ];

  function baseTopic(config) {
    const systemId = (config.systemId || "unbound").trim();
    const deviceClass = (config.deviceClass || "pid").trim();
    const deviceId = (config.deviceId || "").trim();
    return `/systems/${systemId}/devices/${deviceClass}/${deviceId}`;
  }

  function topicFor(config, leaf) {
    return `${baseTopic(config)}${leaf || ""}`;
  }

  function createCmdId(command) {
    return `${command}_${Date.now()}`;
  }

  function parseJsonIfNeeded(value) {
    if (typeof value !== "string") return value;
    try {
      return JSON.parse(value);
    } catch (_) {
      return value;
    }
  }

  function asFiniteNumber(value) {
    const num = Number(value);
    return Number.isFinite(num) ? num : null;
  }

  function asIntegerInRange(value, min, max) {
    const num = Number(value);
    if (!Number.isInteger(num) || num < min || num > max) return null;
    return num;
  }

  function asNumberInRange(value, min, max) {
    const num = asFiniteNumber(value);
    if (num === null || num < min || num > max) return null;
    return num;
  }

  function buildSignalPayload(command) {
    return function() {
      return { command, cmdId: createCmdId(command) };
    };
  }

  function buildIntegerFieldPayload(command, field, min, max) {
    return function(msg) {
      const value = asIntegerInRange(msg.payload, min, max);
      if (value === null) throw new Error(`${command} requires an integer ${field} in range ${min}..${max}`);
      const out = { command, cmdId: createCmdId(command) };
      out[field] = value;
      return out;
    };
  }

  function buildFloatFieldPayload(command, field, min, max) {
    return function(msg) {
      const value = asNumberInRange(msg.payload, min, max);
      if (value === null) throw new Error(`${command} requires a numeric ${field} in range ${min}..${max}`);
      const out = { command, cmdId: createCmdId(command) };
      out[field] = value;
      return out;
    };
  }

  function buildEnumPayload(command, field, allowed) {
    return function(msg) {
      const value = Number(msg.payload);
      if (!allowed.includes(value)) throw new Error(`${command} requires ${field} to be one of ${allowed.join(", ")}`);
      const out = { command, cmdId: createCmdId(command) };
      out[field] = value;
      return out;
    };
  }

  function buildSetpointPayload(msg) {
    const value = asFiniteNumber(msg.payload);
    if (value === null) throw new Error("setpoint requires msg.payload to be numeric");
    return { command: "setpoint", setpointC: value, cmdId: createCmdId("setpoint") };
  }

  function buildMinutesPayload(msg) {
    const value = asFiniteNumber(msg.payload);
    if (value === null) throw new Error("minutes requires msg.payload to be numeric");
    return { command: "minutes", minutes: Math.round(value), cmdId: createCmdId("minutes") };
  }

  function buildMqttHostPayload(msg) {
    const value = String(msg.payload == null ? "" : msg.payload).trim();
    if (!value) throw new Error("mqtt_host requires a non-empty string payload");
    return { command: "mqtt_host", host: value, cmdId: createCmdId("mqtt_host") };
  }

  function buildMqttTlsPayload(msg) {
    const raw = typeof msg.payload === "boolean" ? (msg.payload ? 1 : 0) : Number(msg.payload);
    if (!(raw === 0 || raw === 1)) throw new Error("mqtt_tls requires msg.payload of 0, 1, true, or false");
    return { command: "mqtt_tls", enabled: raw, cmdId: createCmdId("mqtt_tls") };
  }

  function buildPidPayload(msg) {
    const payload = parseJsonIfNeeded(msg.payload) || {};
    const kp = asFiniteNumber(payload.kp);
    const ki = asFiniteNumber(payload.ki);
    const kd = asFiniteNumber(payload.kd);
    if (kp === null || ki === null || kd === null) throw new Error("pid requires payload object with numeric kp, ki, kd");
    return { command: "pid", kp, ki, kd, cmdId: createCmdId("pid") };
  }

  function buildTempCalibrationPayload(msg) {
    const payload = parseJsonIfNeeded(msg.payload) || {};
    const offset = asFiniteNumber(payload.tempOffsetC);
    const smoothing = asFiniteNumber(payload.tempSmoothingAlpha);
    if (offset === null && smoothing === null) {
      throw new Error("temp_calibration requires payload.tempOffsetC and/or payload.tempSmoothingAlpha");
    }
    const out = { command: "temp_calibration", cmdId: createCmdId("temp_calibration") };
    if (offset !== null) out.tempOffsetC = offset;
    if (smoothing !== null) out.tempSmoothingAlpha = smoothing;
    return out;
  }

  function buildProfileIndexPayload(command) {
    return function(msg) {
      const index = asIntegerInRange(msg.payload, 0, Number.MAX_SAFE_INTEGER);
      if (index === null) throw new Error(`${command} requires msg.payload to be a non-negative integer index`);
      return { command, index, cmdId: createCmdId(command) };
    };
  }

  function buildProfileUpsertPayload(msg) {
    const payload = parseJsonIfNeeded(msg.payload);
    const profile = payload && payload.profile ? payload.profile : payload;
    if (!profile || typeof profile !== "object" || Array.isArray(profile)) {
      throw new Error("profile_upsert requires payload.profile or payload to be a profile object");
    }
    return { command: "profile_upsert", profile, cmdId: createCmdId("profile_upsert") };
  }

  function buildObjectPayload(name) {
    return function(msg) {
      const payload = parseJsonIfNeeded(msg.payload);
      if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
        throw new Error(`${name} requires msg.payload to be an object`);
      }
      return payload;
    };
  }

  function buildPassthroughObjectPayload(msg) {
    if (msg.payload == null || msg.payload === "") return {};
    const payload = parseJsonIfNeeded(msg.payload);
    if (!payload || typeof payload !== "object" || Array.isArray(payload)) {
      throw new Error("payload must be an object if provided");
    }
    return payload;
  }

  function buildHeartbeatPayload(msg) {
    const payload = parseJsonIfNeeded(msg.payload);
    if (payload && typeof payload === "object" && !Array.isArray(payload)) {
      if (!payload.controller_id) throw new Error("controller heartbeat payload object must contain controller_id");
      return payload;
    }
    const controllerId = String(msg.payload == null ? "" : msg.payload).trim();
    if (!controllerId) throw new Error("controller heartbeat requires controller_id string or object payload");
    return { controller_id: controllerId };
  }

  function ControllerConfigNode(n) {
    RED.nodes.createNode(this, n);
    const node = this;
    node.name = n.name;
    node.brokerHost = n.brokerHost;
    node.brokerPort = Number(n.brokerPort || 1883);
    node.useTls = !!n.useTls;
    node.systemId = n.systemId || "unbound";
    node.deviceClass = n.deviceClass || "pid";
    node.deviceId = n.deviceId || "";
    node.keepalive = Number(n.keepalive || 30);
    node.clientId = n.clientId || "";
    node.credentials = node.credentials || {};
    node._client = null;
    node._connected = false;
    node._statusUsers = new Set();
    node._subscriptions = new Map();

    function notifyStatus(fill, shape, text) {
      node._statusUsers.forEach((user) => {
        if (typeof user.status === "function") user.status({ fill, shape, text });
      });
    }

    function subscribeAll() {
      if (!node._client || !node._connected) return;
      for (const topic of node._subscriptions.keys()) {
        node._client.subscribe(topic, { qos: 0 });
      }
    }

    function connect() {
      const protocol = node.useTls ? "mqtts" : "mqtt";
      const url = `${protocol}://${node.brokerHost}:${node.brokerPort}`;
      const options = {
        keepalive: node.keepalive,
        reconnectPeriod: 5000,
        connectTimeout: 30000,
        clean: true
      };

      if (node.clientId) options.clientId = node.clientId;
      if (node.credentials.username) options.username = node.credentials.username;
      if (node.credentials.password) options.password = node.credentials.password;

      node._client = mqtt.connect(url, options);
      notifyStatus("yellow", "ring", "connecting");

      node._client.on("connect", () => {
        node._connected = true;
        subscribeAll();
        notifyStatus("green", "dot", "connected");
      });

      node._client.on("reconnect", () => {
        node._connected = false;
        notifyStatus("yellow", "ring", "reconnecting");
      });

      node._client.on("close", () => {
        node._connected = false;
        notifyStatus("red", "ring", "disconnected");
      });

      node._client.on("error", (err) => {
        node._connected = false;
        notifyStatus("red", "ring", "error");
        node.error(err);
      });

      node._client.on("message", (topic, payloadBuffer) => {
        const handlers = node._subscriptions.get(topic);
        if (!handlers || handlers.size === 0) return;

        const text = payloadBuffer.toString();
        let payload = text;
        try {
          payload = JSON.parse(text);
        } catch (_) {
          // leave as string
        }

        handlers.forEach((handler) => handler(payload, topic));
      });
    }

    node.baseTopic = function() {
      return baseTopic(node);
    };

    node.topicFor = function(leaf) {
      return topicFor(node, leaf);
    };

    node.registerStatusUser = function(user) {
      node._statusUsers.add(user);
      if (node._connected) user.status({ fill: "green", shape: "dot", text: "connected" });
      else user.status({ fill: "red", shape: "ring", text: "disconnected" });
    };

    node.unregisterStatusUser = function(user) {
      node._statusUsers.delete(user);
    };

    node.publish = function(leaf, payload, callback) {
      if (!node._client || !node._connected) {
        const err = new Error("controller MQTT client is not connected");
        if (callback) callback(err);
        return;
      }

      const topic = node.topicFor(leaf);
      const body = typeof payload === "string" ? payload : JSON.stringify(payload);
      node._client.publish(topic, body, { qos: 0, retain: false }, callback);
    };

    node.subscribe = function(leaf, handler) {
      const topic = node.topicFor(leaf);
      if (!node._subscriptions.has(topic)) node._subscriptions.set(topic, new Set());
      node._subscriptions.get(topic).add(handler);
      if (node._client && node._connected) node._client.subscribe(topic, { qos: 0 });
      return topic;
    };

    node.unsubscribe = function(leaf, handler) {
      const topic = node.topicFor(leaf);
      const handlers = node._subscriptions.get(topic);
      if (!handlers) return;
      handlers.delete(handler);
      if (handlers.size === 0) {
        node._subscriptions.delete(topic);
        if (node._client && node._connected) node._client.unsubscribe(topic);
      }
    };

    connect();

    node.on("close", function(done) {
      if (node._client) {
        try {
          node._client.end(true, done);
          return;
        } catch (_) {
          // fall through
        }
      }
      done();
    });
  }

  RED.nodes.registerType("automation-controller-config", ControllerConfigNode, {
    credentials: {
      username: { type: "text" },
      password: { type: "password" }
    }
  });

  function GenericCommandNode(config, def) {
    RED.nodes.createNode(this, config);
    const node = this;
    node.name = config.name;
    node.controller = RED.nodes.getNode(config.controller);

    if (!node.controller) {
      node.status({ fill: "red", shape: "ring", text: "missing controller" });
      return;
    }

    node.controller.registerStatusUser(node);

    node.on("input", function(msg, send, done) {
      send = send || function() { node.send.apply(node, arguments); };
      try {
        const payload = def.buildPayload(msg || {});
        node.controller.publish(def.leaf, payload, function(err) {
          if (err) {
            node.status({ fill: "red", shape: "ring", text: "publish failed" });
            if (done) done(err);
            else node.error(err, msg);
            return;
          }

          const outMsg = Object.assign({}, msg, {
            topic: node.controller.topicFor(def.leaf),
            payload
          });
          send(outMsg);
          if (done) done();
        });
      } catch (err) {
        if (done) done(err);
        else node.error(err, msg);
      }
    });

    node.on("close", function(done) {
      node.controller.unregisterStatusUser(node);
      done();
    });
  }

  COMMAND_DEFS.forEach((def) => {
    function WrappedCommandNode(config) {
      GenericCommandNode.call(this, config, def);
    }
    RED.nodes.registerType(def.type, WrappedCommandNode);
  });

  function GenericReceiveNode(config, def) {
    RED.nodes.createNode(this, config);
    const node = this;
    node.name = config.name;
    node.controller = RED.nodes.getNode(config.controller);

    if (!node.controller) {
      node.status({ fill: "red", shape: "ring", text: "missing controller" });
      return;
    }

    node.controller.registerStatusUser(node);

    const handler = function(payload, topic) {
      if (def.filterType) {
        if (!payload || typeof payload !== "object" || payload._type !== def.filterType) return;
      }

      node.send({
        topic,
        payload,
        baseTopic: node.controller.baseTopic(),
        leaf: def.leaf
      });
    };

    node.controller.subscribe(def.leaf, handler);

    node.on("close", function(done) {
      node.controller.unsubscribe(def.leaf, handler);
      node.controller.unregisterStatusUser(node);
      done();
    });
  }

  RECEIVE_DEFS.forEach((def) => {
    function WrappedReceiveNode(config) {
      GenericReceiveNode.call(this, config, def);
    }
    RED.nodes.registerType(def.type, WrappedReceiveNode);
  });
};
