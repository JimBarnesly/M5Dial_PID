# AGENTS.md

## Purpose

This repository is the software foundation for a modular commercial control ecosystem for brewing, hydroponics, and similar process-control systems.

The goal is not just to make the current hardware work. The goal is to evolve the codebase toward a reliable, product-ready architecture that supports standalone devices, optional hub supervision, modular expansion, easy provisioning, and future commercial deployment.

---

## Product model

### Device tiers

* A standalone PID controller must work independently with local control.
* A hub/controller can later be added to automate standalone devices, send setpoints, coordinate modules, and run higher-level logic.
* Additional modules may include pump controllers, valve controllers, relay modules, pressure/level sensing modules, and future expansion devices.
* Customers may buy one device first and add more later. The software architecture must support this incremental upgrade path cleanly.

### Core behavioral principles

* Every field device should remain useful standalone where appropriate.
* Local control loops should stay on the field device where appropriate.
* The hub should supervise, coordinate, and automate. It should not make basic local control fragile.
* Devices must have clear local/manual/remote/failsafe behavior.
* If the hub disappears, field devices must behave safely and predictably.

---

## Networking model

### Current intended model

* The hub/controller is the authority for the system.
* The hub should normally connect upstream by Ethernet when available.
* The hub should run its own Wi-Fi AP for field devices.
* Field devices should connect to the hub AP, not arbitrary home/venue Wi-Fi, during normal system operation.
* The field-device network should use stable, predictable addressing.
* The hub AP subnet is intended to be fixed.
* The hub AP IP should be stable, e.g. `10.42.0.1`.
* MQTT should be reachable on the hub AP IP.

### Optional upstream connectivity

* The hub may later support upstream Wi-Fi via a second Wi-Fi interface or dongle.
* The hub needs upstream internet/LAN access for dashboards, tablet access, Brewfather sync, updates, and future integrations.
* External Wi-Fi should be treated as an uplink, not the foundation of field-device communications.

### Networking rules

* Do not assume `.local` name resolution is always available or reliable.
* Do not design normal device operation around users manually entering broker IPs forever.
* Prefer stable internal addresses for field control paths.
* Prefer system-owned networking over ad hoc user-entered infrastructure details.

---

## Provisioning and pairing direction

### Desired provisioning model

* Pairing and onboarding should be easy and deliberate.
* Installed devices may be physically fixed in place, so physical hub-to-device contact is not always practical.
* The intended direction is that each hub ships with an NFC commissioning card.
* The hub writes system bootstrap data to the card.
* The user takes the card to each device and taps it.
* The device receives enough information to join the correct hub/system.

### Pairing principles

* The commissioning card is a bootstrap token for system enrollment, not just a convenience gimmick.
* Pairing should prevent accidental cross-binding when multiple systems exist in the same venue.
* Devices should bind to one system/hub until deliberately reset or re-paired.
* Avoid architectures that rely on one globally hardcoded hostname for all shipped systems.
* Distinguish between:

  * a stable machine-safe system identity
  * a user-friendly display/system name

### Identity rules

* Do not use the display name as the only machine identifier.
* Prefer unique system IDs, hub IDs, and device IDs.
* Prefer internal IDs for topics and system structure.
* Use friendly names only for display and UX.

---

## MQTT and device architecture

### MQTT expectations

* MQTT must support multiple device classes over time.
* Topic structure should scale to a modular ecosystem, not just a single prototype device.
* Broker location assumptions should be explicit and resilient.
* Device identity, system identity, and hub identity should be cleanly separated.
* Avoid topic structures that will become brittle once multiple device types are present.

### Device-role expectations

* The codebase should evolve toward explicit support for multiple device roles.
* Avoid hardcoding logic that assumes a single controller type forever.
* Shared infrastructure should be reusable across device classes.

---

## Persistence and state management

### Persistence expectations

* Settings persistence must be explicit, reliable, and debuggable.
* Boot-time restoration must be predictable.
* Avoid stale-config behavior that makes flashed firmware appear unchanged.
* Be careful with Preferences/NVS, WiFiManager persistence, and reconnect logic.
* Reconnect behavior after reset/power cycle is important and should be treated as a product concern, not an afterthought.

### Configuration rules

* Prefer clear ownership of configuration data.
* Avoid hidden coupling between WiFiManager, runtime config, and stored settings.
* When possible, keep configuration schemas simple, explicit, and evolvable.

---

## Engineering priorities

When reviewing or changing code, prioritize:

1. Correctness
2. Reliability
3. Safe control behavior
4. Maintainability
5. Clear separation of concerns
6. Product-ready architecture over prototype shortcuts

Do not optimize for short-term convenience if it makes future modularity, provisioning, identity, or reliability worse.

---

## What to look for during reviews

Flag and explain:

* bugs
* design flaws
* technical debt that blocks productization
* hidden coupling
* stale assumptions tied to one SSID, one IP, one device, or one deployment mode
* anything that makes standalone-first then hub-upgrade-later behavior harder
* anything that makes the future hub/AP/NFC commissioning model harder

Do not waste time on trivial style issues unless they materially affect maintainability.

---

## Preferred review style

* Be concrete.
* Be repo-aware.
* Reference files, classes, functions, and line ranges where possible.
* Prefer surgical improvements and practical migration paths.
* Avoid suggesting a ground-up rewrite unless absolutely necessary.
* Distinguish clearly between prototype-acceptable decisions and commercial-product risks.

---

## Short practical guidance for agents

When making recommendations or changes:

* Preserve standalone operation where appropriate.
* Avoid introducing unnecessary dependence on the hub for basic local control.
* Prefer stable internal networking assumptions for field devices.
* Prefer server addresses stored as strings when flexibility is needed.
* Be cautious with WiFiManager and reconnect state.
* Assume field devices may be installed before the hub exists.
* Assume the system must be portable between sites.
* Assume multiple systems may coexist in the same venue.
* Keep the path open for future NFC card bootstrap provisioning.

---

## Non-goals

Do not steer the architecture toward:

* permanent reliance on manual broker IP entry for each device
* a single universal hostname across all shipped systems
* dependence on `.local` alone for core field-device operation
* designs where external venue/home Wi-Fi is the core control backbone
* designs that remove useful standalone behavior from field devices

---

## Default mindset

Treat this repository as an evolving product platform, not just a one-off ESP32 prototype.

Favor decisions that make the ecosystem:

* modular
* portable
* understandable
* debuggable
* scalable across device types
* supportable in the field
* commercially viable
