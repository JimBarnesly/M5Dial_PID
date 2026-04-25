# Node-RED Automation Controller Nodes

This package provides installable Node-RED nodes for the firmware in this repository.

## What it includes

- one shared controller config node
- direct MQTT command nodes
- telemetry receive nodes
- no dependency on separate Node-RED MQTT in/out nodes for normal use

## Install

From your Node-RED user directory:

```bash
npm install /path/to/Node-Red
```

Then restart Node-RED.

## Controller config node

The config node stores:

- broker host
- broker port
- TLS on/off
- username/password
- system ID
- device class
- device ID

These fields determine the topic base:

```text
/systems/<system_id>/devices/<device_class>/<device_id>
```

## Notes

- `device_class` defaults to `pid`
- command nodes publish directly to the firmware MQTT topics
- receive nodes subscribe directly to the firmware MQTT topics
- command nodes accept input on `msg.payload`

