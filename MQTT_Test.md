# MQTT Test Pack for Node-RED

This file provides **Node-RED importable JSON snippets** for the current firmware MQTT command surface.

How to use:

- Copy one fenced `json` block at a time.
- In Node-RED, use `Import`.
- Replace the placeholder topic base `"/systems/unbound/devices/pid/pid-XXXXXX/cmd"` with your real device topic.
- After import, assign your MQTT broker in the `mqtt out` node if it is blank.

These examples are testing-oriented and intentionally repetitive so each command can be imported independently.

## Runtime Control

### Setpoint

Use this to send a numeric setpoint in `C`.

```json
[
  {
    "id": "a1001",
    "type": "tab",
    "label": "MQTT Test - Setpoint",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a1002",
    "type": "inject",
    "z": "a1001",
    "name": "Setpoint Input",
    "props": [
      { "p": "payload" }
    ],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "65",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a1003"]]
  },
  {
    "id": "a1003",
    "type": "function",
    "z": "a1001",
    "name": "Build Setpoint Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value)) {\n    node.warn('Setpoint input is not a valid number');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'setpoint',\n    setpointC: value,\n    cmdId: `setpoint_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a1004"]]
  },
  {
    "id": "a1004",
    "type": "mqtt out",
    "z": "a1001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### Stage Time Minutes

Use this to set the stage time in minutes.

```json
[
  {
    "id": "a1101",
    "type": "tab",
    "label": "MQTT Test - Minutes",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a1102",
    "type": "inject",
    "z": "a1101",
    "name": "Minutes Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "60",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a1103"]]
  },
  {
    "id": "a1103",
    "type": "function",
    "z": "a1101",
    "name": "Build Minutes Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value)) {\n    node.warn('Stage time input is not a valid number');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'minutes',\n    minutes: Math.round(value),\n    cmdId: `minutes_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a1104"]]
  },
  {
    "id": "a1104",
    "type": "mqtt out",
    "z": "a1101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### Start

```json
[
  {
    "id": "a1201",
    "type": "tab",
    "label": "MQTT Test - Start",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a1202",
    "type": "inject",
    "z": "a1201",
    "name": "Start",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a1203"]]
  },
  {
    "id": "a1203",
    "type": "function",
    "z": "a1201",
    "name": "Build Start Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'start',\n    cmdId: `start_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a1204"]]
  },
  {
    "id": "a1204",
    "type": "mqtt out",
    "z": "a1201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### Pause

```json
[
  {
    "id": "a1301",
    "type": "tab",
    "label": "MQTT Test - Pause",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a1302",
    "type": "inject",
    "z": "a1301",
    "name": "Pause",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a1303"]]
  },
  {
    "id": "a1303",
    "type": "function",
    "z": "a1301",
    "name": "Build Pause Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'pause',\n    cmdId: `pause_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a1304"]]
  },
  {
    "id": "a1304",
    "type": "mqtt out",
    "z": "a1301",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### Stop

```json
[
  {
    "id": "a1401",
    "type": "tab",
    "label": "MQTT Test - Stop",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a1402",
    "type": "inject",
    "z": "a1401",
    "name": "Stop",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a1403"]]
  },
  {
    "id": "a1403",
    "type": "function",
    "z": "a1401",
    "name": "Build Stop Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'stop',\n    cmdId: `stop_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a1404"]]
  },
  {
    "id": "a1404",
    "type": "mqtt out",
    "z": "a1401",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

## Alarm Commands

### Acknowledge Alarm

```json
[
  {
    "id": "a2001",
    "type": "tab",
    "label": "MQTT Test - Ack Alarm",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a2002",
    "type": "inject",
    "z": "a2001",
    "name": "Ack Alarm",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a2003"]]
  },
  {
    "id": "a2003",
    "type": "function",
    "z": "a2001",
    "name": "Build Ack Alarm Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'ack_alarm',\n    cmdId: `ack_alarm_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 460,
    "y": 80,
    "wires": [["a2004"]]
  },
  {
    "id": "a2004",
    "type": "mqtt out",
    "z": "a2001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### Reset Alarm

```json
[
  {
    "id": "a2101",
    "type": "tab",
    "label": "MQTT Test - Reset Alarm",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a2102",
    "type": "inject",
    "z": "a2101",
    "name": "Reset Alarm",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a2103"]]
  },
  {
    "id": "a2103",
    "type": "function",
    "z": "a2101",
    "name": "Build Reset Alarm Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'reset_alarm',\n    cmdId: `reset_alarm_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a2104"]]
  },
  {
    "id": "a2104",
    "type": "mqtt out",
    "z": "a2101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

## Config and Network Commands

### MQTT Host

```json
[
  {
    "id": "a3001",
    "type": "tab",
    "label": "MQTT Test - MQTT Host",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3002",
    "type": "inject",
    "z": "a3001",
    "name": "MQTT Host Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "10.42.0.1",
    "payloadType": "str",
    "x": 170,
    "y": 80,
    "wires": [["a3003"]]
  },
  {
    "id": "a3003",
    "type": "function",
    "z": "a3001",
    "name": "Build MQTT Host Command",
    "func": "const value = String(msg.payload || '').trim();\nif (!value) {\n    node.warn('MQTT host is empty');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'mqtt_host',\n    host: value,\n    cmdId: `mqtt_host_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a3004"]]
  },
  {
    "id": "a3004",
    "type": "mqtt out",
    "z": "a3001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

### MQTT Port

```json
[
  {
    "id": "a3101",
    "type": "tab",
    "label": "MQTT Test - MQTT Port",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3102",
    "type": "inject",
    "z": "a3101",
    "name": "MQTT Port Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "1883",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a3103"]]
  },
  {
    "id": "a3103",
    "type": "function",
    "z": "a3101",
    "name": "Build MQTT Port Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 1 || value > 65535) {\n    node.warn('MQTT port must be 1..65535');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'mqtt_port',\n    port: value,\n    cmdId: `mqtt_port_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a3104"]]
  },
  {
    "id": "a3104",
    "type": "mqtt out",
    "z": "a3101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

### MQTT TLS

```json
[
  {
    "id": "a3201",
    "type": "tab",
    "label": "MQTT Test - MQTT TLS",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3202",
    "type": "inject",
    "z": "a3201",
    "name": "TLS Enabled 0 or 1",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0",
    "payloadType": "num",
    "x": 180,
    "y": 80,
    "wires": [["a3203"]]
  },
  {
    "id": "a3203",
    "type": "function",
    "z": "a3201",
    "name": "Build MQTT TLS Command",
    "func": "const value = Number(msg.payload);\nif (!(value === 0 || value === 1)) {\n    node.warn('MQTT TLS must be 0 or 1');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'mqtt_tls',\n    enabled: value,\n    cmdId: `mqtt_tls_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a3204"]]
  },
  {
    "id": "a3204",
    "type": "mqtt out",
    "z": "a3201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

### MQTT Timeout

```json
[
  {
    "id": "a3301",
    "type": "tab",
    "label": "MQTT Test - MQTT Timeout",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3302",
    "type": "inject",
    "z": "a3301",
    "name": "Timeout Seconds",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "30",
    "payloadType": "num",
    "x": 180,
    "y": 80,
    "wires": [["a3303"]]
  },
  {
    "id": "a3303",
    "type": "function",
    "z": "a3301",
    "name": "Build MQTT Timeout Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 0 || value > 3600) {\n    node.warn('MQTT timeout must be 0..3600');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'mqtt_timeout',\n    seconds: value,\n    cmdId: `mqtt_timeout_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 490,
    "y": 80,
    "wires": [["a3304"]]
  },
  {
    "id": "a3304",
    "type": "mqtt out",
    "z": "a3301",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 720,
    "y": 80,
    "wires": []
  }
]
```

### MQTT Fallback Mode

Mode values: `0 = hold_setpoint`, `1 = pause`, `2 = stop_heater`

```json
[
  {
    "id": "a3401",
    "type": "tab",
    "label": "MQTT Test - MQTT Fallback",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3402",
    "type": "inject",
    "z": "a3401",
    "name": "Fallback Mode",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a3403"]]
  },
  {
    "id": "a3403",
    "type": "function",
    "z": "a3401",
    "name": "Build MQTT Fallback Command",
    "func": "const value = Number(msg.payload);\nif (![0,1,2].includes(value)) {\n    node.warn('MQTT fallback must be 0, 1, or 2');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'mqtt_fallback',\n    mode: value,\n    cmdId: `mqtt_fallback_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a3404"]]
  },
  {
    "id": "a3404",
    "type": "mqtt out",
    "z": "a3401",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 730,
    "y": 80,
    "wires": []
  }
]
```

### Wi-Fi Portal Timeout

```json
[
  {
    "id": "a3501",
    "type": "tab",
    "label": "MQTT Test - WiFi Portal Timeout",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3502",
    "type": "inject",
    "z": "a3501",
    "name": "Portal Timeout Seconds",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "180",
    "payloadType": "num",
    "x": 190,
    "y": 80,
    "wires": [["a3503"]]
  },
  {
    "id": "a3503",
    "type": "function",
    "z": "a3501",
    "name": "Build WiFi Timeout Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 30 || value > 1800) {\n    node.warn('WiFi portal timeout must be 30..1800');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'wifi_portal_timeout',\n    seconds: value,\n    cmdId: `wifi_portal_timeout_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a3504"]]
  },
  {
    "id": "a3504",
    "type": "mqtt out",
    "z": "a3501",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 730,
    "y": 80,
    "wires": []
  }
]
```

### Reset Wi-Fi

```json
[
  {
    "id": "a3601",
    "type": "tab",
    "label": "MQTT Test - Reset WiFi",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3602",
    "type": "inject",
    "z": "a3601",
    "name": "Reset WiFi",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a3603"]]
  },
  {
    "id": "a3603",
    "type": "function",
    "z": "a3601",
    "name": "Build Reset WiFi Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'reset_wifi',\n    cmdId: `reset_wifi_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a3604"]]
  },
  {
    "id": "a3604",
    "type": "mqtt out",
    "z": "a3601",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

### Over Temperature Limit

```json
[
  {
    "id": "a3701",
    "type": "tab",
    "label": "MQTT Test - Over Temp",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3702",
    "type": "inject",
    "z": "a3701",
    "name": "Over Temp Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "99",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a3703"]]
  },
  {
    "id": "a3703",
    "type": "function",
    "z": "a3701",
    "name": "Build Over Temp Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value) || value < 20 || value > 140) {\n    node.warn('Over temp must be 20..140');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'over_temp',\n    overTempC: value,\n    cmdId: `over_temp_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a3704"]]
  },
  {
    "id": "a3704",
    "type": "mqtt out",
    "z": "a3701",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 710,
    "y": 80,
    "wires": []
  }
]
```

### Control Lock

Values: `0 = local_only`, `1 = remote_only`, `2 = local_or_remote`

```json
[
  {
    "id": "a3801",
    "type": "tab",
    "label": "MQTT Test - Control Lock",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a3802",
    "type": "inject",
    "z": "a3801",
    "name": "Control Lock Value",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "2",
    "payloadType": "num",
    "x": 180,
    "y": 80,
    "wires": [["a3803"]]
  },
  {
    "id": "a3803",
    "type": "function",
    "z": "a3801",
    "name": "Build Control Lock Command",
    "func": "const value = Number(msg.payload);\nif (![0,1,2].includes(value)) {\n    node.warn('Control lock must be 0, 1, or 2');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'control_lock',\n    controlLock: value,\n    cmdId: `control_lock_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 490,
    "y": 80,
    "wires": [["a3804"]]
  },
  {
    "id": "a3804",
    "type": "mqtt out",
    "z": "a3801",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 720,
    "y": 80,
    "wires": []
  }
]
```

## PID Commands

### Full PID Update

```json
[
  {
    "id": "a4001",
    "type": "tab",
    "label": "MQTT Test - PID",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a4002",
    "type": "inject",
    "z": "a4001",
    "name": "PID JSON",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"kp\":18,\"ki\":0.08,\"kd\":20}",
    "payloadType": "json",
    "x": 160,
    "y": 80,
    "wires": [["a4003"]]
  },
  {
    "id": "a4003",
    "type": "function",
    "z": "a4001",
    "name": "Build PID Command",
    "func": "const payload = msg.payload || {};\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'pid',\n    kp: Number(payload.kp),\n    ki: Number(payload.ki),\n    kd: Number(payload.kd),\n    cmdId: `pid_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 430,
    "y": 80,
    "wires": [["a4004"]]
  },
  {
    "id": "a4004",
    "type": "mqtt out",
    "z": "a4001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 680,
    "y": 80,
    "wires": []
  }
]
```

### PID Kp

```json
[
  {
    "id": "a4101",
    "type": "tab",
    "label": "MQTT Test - PID Kp",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a4102",
    "type": "inject",
    "z": "a4101",
    "name": "Kp Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "18",
    "payloadType": "num",
    "x": 160,
    "y": 80,
    "wires": [["a4103"]]
  },
  {
    "id": "a4103",
    "type": "function",
    "z": "a4101",
    "name": "Build PID Kp Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value) || value <= 0 || value > 60) {\n    node.warn('Kp must be > 0 and <= 60');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'pid_kp',\n    kp: value,\n    cmdId: `pid_kp_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a4104"]]
  },
  {
    "id": "a4104",
    "type": "mqtt out",
    "z": "a4101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### PID Ki

```json
[
  {
    "id": "a4201",
    "type": "tab",
    "label": "MQTT Test - PID Ki",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a4202",
    "type": "inject",
    "z": "a4201",
    "name": "Ki Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0.08",
    "payloadType": "num",
    "x": 160,
    "y": 80,
    "wires": [["a4203"]]
  },
  {
    "id": "a4203",
    "type": "function",
    "z": "a4201",
    "name": "Build PID Ki Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value) || value <= 0 || value > 2) {\n    node.warn('Ki must be > 0 and <= 2');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'pid_ki',\n    ki: value,\n    cmdId: `pid_ki_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a4204"]]
  },
  {
    "id": "a4204",
    "type": "mqtt out",
    "z": "a4201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

### PID Kd

```json
[
  {
    "id": "a4301",
    "type": "tab",
    "label": "MQTT Test - PID Kd",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a4302",
    "type": "inject",
    "z": "a4301",
    "name": "Kd Input",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "20",
    "payloadType": "num",
    "x": 160,
    "y": 80,
    "wires": [["a4303"]]
  },
  {
    "id": "a4303",
    "type": "function",
    "z": "a4301",
    "name": "Build PID Kd Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isFinite(value) || value <= 0 || value > 80) {\n    node.warn('Kd must be > 0 and <= 80');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'pid_kd',\n    kd: value,\n    cmdId: `pid_kd_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 450,
    "y": 80,
    "wires": [["a4304"]]
  },
  {
    "id": "a4304",
    "type": "mqtt out",
    "z": "a4301",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 700,
    "y": 80,
    "wires": []
  }
]
```

## Autotune Commands

### Start Autotune

```json
[
  {
    "id": "a5001",
    "type": "tab",
    "label": "MQTT Test - Start Autotune",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a5002",
    "type": "inject",
    "z": "a5001",
    "name": "Start Autotune",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a5003"]]
  },
  {
    "id": "a5003",
    "type": "function",
    "z": "a5001",
    "name": "Build Start Autotune Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'start_autotune',\n    cmdId: `start_autotune_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a5004"]]
  },
  {
    "id": "a5004",
    "type": "mqtt out",
    "z": "a5001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Accept Tune

```json
[
  {
    "id": "a5101",
    "type": "tab",
    "label": "MQTT Test - Accept Tune",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a5102",
    "type": "inject",
    "z": "a5101",
    "name": "Accept Tune",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a5103"]]
  },
  {
    "id": "a5103",
    "type": "function",
    "z": "a5101",
    "name": "Build Accept Tune Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'accept_tune',\n    cmdId: `accept_tune_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 490,
    "y": 80,
    "wires": [["a5104"]]
  },
  {
    "id": "a5104",
    "type": "mqtt out",
    "z": "a5101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 740,
    "y": 80,
    "wires": []
  }
]
```

### Reject Tune

```json
[
  {
    "id": "a5201",
    "type": "tab",
    "label": "MQTT Test - Reject Tune",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a5202",
    "type": "inject",
    "z": "a5201",
    "name": "Reject Tune",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a5203"]]
  },
  {
    "id": "a5203",
    "type": "function",
    "z": "a5201",
    "name": "Build Reject Tune Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'reject_tune',\n    cmdId: `reject_tune_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 490,
    "y": 80,
    "wires": [["a5204"]]
  },
  {
    "id": "a5204",
    "type": "mqtt out",
    "z": "a5201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 740,
    "y": 80,
    "wires": []
  }
]
```

## Calibration Commands

### Temperature Calibration

```json
[
  {
    "id": "a6001",
    "type": "tab",
    "label": "MQTT Test - Temp Calibration",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a6002",
    "type": "inject",
    "z": "a6001",
    "name": "Calibration JSON",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"tempOffsetC\":0.3,\"tempSmoothingAlpha\":0.25}",
    "payloadType": "json",
    "x": 170,
    "y": 80,
    "wires": [["a6003"]]
  },
  {
    "id": "a6003",
    "type": "function",
    "z": "a6001",
    "name": "Build Temp Calibration Command",
    "func": "const payload = msg.payload || {};\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'temp_calibration',\n    tempOffsetC: Number(payload.tempOffsetC),\n    tempSmoothingAlpha: Number(payload.tempSmoothingAlpha),\n    cmdId: `temp_calibration_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 510,
    "y": 80,
    "wires": [["a6004"]]
  },
  {
    "id": "a6004",
    "type": "mqtt out",
    "z": "a6001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 770,
    "y": 80,
    "wires": []
  }
]
```

### Calibration Status Request

```json
[
  {
    "id": "a6101",
    "type": "tab",
    "label": "MQTT Test - Calibration Status",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a6102",
    "type": "inject",
    "z": "a6101",
    "name": "Calibration Status",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 180,
    "y": 80,
    "wires": [["a6103"]]
  },
  {
    "id": "a6103",
    "type": "function",
    "z": "a6101",
    "name": "Build Calibration Status Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'calibration_status',\n    cmdId: `calibration_status_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 520,
    "y": 80,
    "wires": [["a6104"]]
  },
  {
    "id": "a6104",
    "type": "mqtt out",
    "z": "a6101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 790,
    "y": 80,
    "wires": []
  }
]
```

## Readback Commands

### Get Config

```json
[
  {
    "id": "a7001",
    "type": "tab",
    "label": "MQTT Test - Get Config",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a7002",
    "type": "inject",
    "z": "a7001",
    "name": "Get Config",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a7003"]]
  },
  {
    "id": "a7003",
    "type": "function",
    "z": "a7001",
    "name": "Build Get Config Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'get_config',\n    cmdId: `get_config_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 480,
    "y": 80,
    "wires": [["a7004"]]
  },
  {
    "id": "a7004",
    "type": "mqtt out",
    "z": "a7001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 730,
    "y": 80,
    "wires": []
  }
]
```

### Get Events

```json
[
  {
    "id": "a7101",
    "type": "tab",
    "label": "MQTT Test - Get Events",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a7102",
    "type": "inject",
    "z": "a7101",
    "name": "Get Events",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a7103"]]
  },
  {
    "id": "a7103",
    "type": "function",
    "z": "a7101",
    "name": "Build Get Events Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'get_events',\n    cmdId: `get_events_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 480,
    "y": 80,
    "wires": [["a7104"]]
  },
  {
    "id": "a7104",
    "type": "mqtt out",
    "z": "a7101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 730,
    "y": 80,
    "wires": []
  }
]
```

## Profile Commands

### Profile Select

```json
[
  {
    "id": "a8001",
    "type": "tab",
    "label": "MQTT Test - Profile Select",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a8002",
    "type": "inject",
    "z": "a8001",
    "name": "Profile Index",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a8003"]]
  },
  {
    "id": "a8003",
    "type": "function",
    "z": "a8001",
    "name": "Build Profile Select Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 0) {\n    node.warn('Profile index must be >= 0');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'profile_select',\n    index: value,\n    cmdId: `profile_select_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a8004"]]
  },
  {
    "id": "a8004",
    "type": "mqtt out",
    "z": "a8001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Profile Start

```json
[
  {
    "id": "a8101",
    "type": "tab",
    "label": "MQTT Test - Profile Start",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a8102",
    "type": "inject",
    "z": "a8101",
    "name": "Profile Index",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a8103"]]
  },
  {
    "id": "a8103",
    "type": "function",
    "z": "a8101",
    "name": "Build Profile Start Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 0) {\n    node.warn('Profile index must be >= 0');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'profile_start',\n    index: value,\n    cmdId: `profile_start_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a8104"]]
  },
  {
    "id": "a8104",
    "type": "mqtt out",
    "z": "a8101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Profile Delete

```json
[
  {
    "id": "a8201",
    "type": "tab",
    "label": "MQTT Test - Profile Delete",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a8202",
    "type": "inject",
    "z": "a8201",
    "name": "Profile Index",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "0",
    "payloadType": "num",
    "x": 170,
    "y": 80,
    "wires": [["a8203"]]
  },
  {
    "id": "a8203",
    "type": "function",
    "z": "a8201",
    "name": "Build Profile Delete Command",
    "func": "const value = Number(msg.payload);\nif (!Number.isInteger(value) || value < 0) {\n    node.warn('Profile index must be >= 0');\n    return null;\n}\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'profile_delete',\n    index: value,\n    cmdId: `profile_delete_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a8204"]]
  },
  {
    "id": "a8204",
    "type": "mqtt out",
    "z": "a8201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Profile Upsert

```json
[
  {
    "id": "a8301",
    "type": "tab",
    "label": "MQTT Test - Profile Upsert",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a8302",
    "type": "inject",
    "z": "a8301",
    "name": "Profile JSON",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"profile\":{\"index\":0,\"name\":\"PROFILE_1\",\"stages\":[{\"name\":\"STEP1\",\"targetC\":65,\"holdSeconds\":1800},{\"name\":\"STEP2\",\"targetC\":72,\"holdSeconds\":1200}]}}",
    "payloadType": "json",
    "x": 170,
    "y": 80,
    "wires": [["a8303"]]
  },
  {
    "id": "a8303",
    "type": "function",
    "z": "a8301",
    "name": "Build Profile Upsert Command",
    "func": "const payload = msg.payload || {};\nmsg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd';\nmsg.payload = {\n    command: 'profile_upsert',\n    profile: payload.profile,\n    cmdId: `profile_upsert_${Date.now()}`\n};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 510,
    "y": 80,
    "wires": [["a8304"]]
  },
  {
    "id": "a8304",
    "type": "mqtt out",
    "z": "a8301",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 780,
    "y": 80,
    "wires": []
  }
]
```

## Integration and Development Helpers

### Pairing Mode

```json
[
  {
    "id": "a9001",
    "type": "tab",
    "label": "MQTT Test - Pairing Mode",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a9002",
    "type": "inject",
    "z": "a9001",
    "name": "Open Pairing Window",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 180,
    "y": 80,
    "wires": [["a9003"]]
  },
  {
    "id": "a9003",
    "type": "function",
    "z": "a9001",
    "name": "Build Pairing Mode Command",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd/pairing_mode';\nmsg.payload = {};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a9004"]]
  },
  {
    "id": "a9004",
    "type": "mqtt out",
    "z": "a9001",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Bootstrap Inject

Development-only helper. Signature is currently `dev-allow`.

```json
[
  {
    "id": "a9101",
    "type": "tab",
    "label": "MQTT Test - Bootstrap Inject",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a9102",
    "type": "inject",
    "z": "a9101",
    "name": "Bootstrap JSON",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"version\":1,\"system_id\":\"system_a\",\"system_name\":\"System A\",\"controller_id\":\"controller_a\",\"controller_public_key\":\"dev-key\",\"ap_ssid\":\"project6\",\"ap_psk\":\"sIlver@99\",\"broker_host\":\"10.42.0.1\",\"broker_port\":1883,\"issued_at\":123456,\"epoch\":1,\"signature\":\"dev-allow\"}",
    "payloadType": "json",
    "x": 180,
    "y": 80,
    "wires": [["a9103"]]
  },
  {
    "id": "a9103",
    "type": "function",
    "z": "a9101",
    "name": "Build Bootstrap Inject Topic",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd/bootstrap_inject';\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 510,
    "y": 80,
    "wires": [["a9104"]]
  },
  {
    "id": "a9104",
    "type": "mqtt out",
    "z": "a9101",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```

### Unpair

```json
[
  {
    "id": "a9201",
    "type": "tab",
    "label": "MQTT Test - Unpair",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a9202",
    "type": "inject",
    "z": "a9201",
    "name": "Unpair",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "",
    "payloadType": "date",
    "x": 170,
    "y": 80,
    "wires": [["a9203"]]
  },
  {
    "id": "a9203",
    "type": "function",
    "z": "a9201",
    "name": "Build Unpair Topic",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/cmd/unpair';\nmsg.payload = {};\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 470,
    "y": 80,
    "wires": [["a9204"]]
  },
  {
    "id": "a9204",
    "type": "mqtt out",
    "z": "a9201",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 720,
    "y": 80,
    "wires": []
  }
]
```

### Enrollment Response

This is published by the controller to complete integrated enrollment.

```json
[
  {
    "id": "a9301",
    "type": "tab",
    "label": "MQTT Test - Enrollment Response",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a9302",
    "type": "inject",
    "z": "a9301",
    "name": "Enrollment Response",
    "props": [{ "p": "payload" }],
    "repeat": "",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"accepted\":true,\"system_id\":\"system_a\",\"controller_id\":\"controller_a\",\"controller_fingerprint\":\"dev-key-fingerprint\"}",
    "payloadType": "json",
    "x": 180,
    "y": 80,
    "wires": [["a9303"]]
  },
  {
    "id": "a9303",
    "type": "function",
    "z": "a9301",
    "name": "Build Enrollment Response Topic",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/enrollment/response';\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 540,
    "y": 80,
    "wires": [["a9304"]]
  },
  {
    "id": "a9304",
    "type": "mqtt out",
    "z": "a9301",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 820,
    "y": 80,
    "wires": []
  }
]
```

### Controller Heartbeat

Publish this often enough to keep integrated supervision alive.

```json
[
  {
    "id": "a9401",
    "type": "tab",
    "label": "MQTT Test - Controller Heartbeat",
    "disabled": false,
    "info": ""
  },
  {
    "id": "a9402",
    "type": "inject",
    "z": "a9401",
    "name": "Heartbeat",
    "props": [{ "p": "payload" }],
    "repeat": "10",
    "crontab": "",
    "once": false,
    "onceDelay": 0.1,
    "topic": "",
    "payload": "{\"controller_id\":\"controller_a\"}",
    "payloadType": "json",
    "x": 170,
    "y": 80,
    "wires": [["a9403"]]
  },
  {
    "id": "a9403",
    "type": "function",
    "z": "a9401",
    "name": "Build Heartbeat Topic",
    "func": "msg.topic = '/systems/unbound/devices/pid/pid-XXXXXX/controller/heartbeat';\nreturn msg;",
    "outputs": 1,
    "timeout": "",
    "noerr": 0,
    "initialize": "",
    "finalize": "",
    "libs": [],
    "x": 500,
    "y": 80,
    "wires": [["a9404"]]
  },
  {
    "id": "a9404",
    "type": "mqtt out",
    "z": "a9401",
    "name": "MQTT Out",
    "topic": "",
    "qos": "",
    "retain": "",
    "respTopic": "",
    "contentType": "",
    "userProps": "",
    "correl": "",
    "expiry": "",
    "broker": "",
    "x": 760,
    "y": 80,
    "wires": []
  }
]
```
