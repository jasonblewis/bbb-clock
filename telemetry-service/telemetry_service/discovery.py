"""Home Assistant MQTT discovery payloads and the state blob.

One HA *device* ("BBB Clock") groups all entities. Discovery configs are
published once, retained; the state is a single JSON blob published each cadence,
NOT retained (we want live values, not a stale reading resurrected on HA
restart). See the "MQTT design" section of issue #8.

Each entity's `key` doubles as its state-JSON field name and its
value_template lookup (value_json.<key>), so the two stay in lockstep.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from . import __version__


@dataclass(frozen=True)
class Entity:
    key: str  # state-JSON field + unique_id suffix + value_json.<key>
    name: str
    unit: str = ""
    device_class: str = ""
    state_class: str = "measurement"
    entity_category: str = ""  # "" for primary; "diagnostic" for system health
    icon: str = ""


# v1 entities (issue #8 "Home Assistant device & entities" table). CPU temp is
# included but only published when the kernel actually exposes a thermal zone —
# HA simply shows it unavailable otherwise.
ENTITIES = [
    Entity("lux", "Ambient lux", unit="lx", device_class="illuminance"),
    Entity("brightness", "Display brightness", unit="%", icon="mdi:brightness-6"),
    Entity("uptime_s", "Uptime", unit="s", device_class="duration",
           state_class="", entity_category="diagnostic"),
    Entity("cpu_temp", "CPU temperature", unit="°C", device_class="temperature",
           entity_category="diagnostic"),
    Entity("wifi_rssi", "WiFi signal", unit="dBm", device_class="signal_strength",
           entity_category="diagnostic"),
    Entity("ssid", "SSID", state_class="", entity_category="diagnostic",
           icon="mdi:wifi"),
    Entity("load1", "Load average", icon="mdi:gauge", entity_category="diagnostic"),
    Entity("mem_free_kb", "Free memory", unit="kB", device_class="data_size",
           entity_category="diagnostic"),
]


def device_block(cfg) -> dict:
    """The `device` block shared by every entity so HA groups them as one device."""
    return {
        "identifiers": [cfg.node_id],
        "name": "BBB Clock",
        "model": "BeagleBone Black LED clock",
        "manufacturer": "jasonblewis/bbb-clock",
        "sw_version": __version__,
    }


def discovery_topic(cfg, entity: Entity) -> str:
    return f"{cfg.discovery_prefix}/sensor/{cfg.node_id}/{entity.key}/config"


def discovery_payload(cfg, entity: Entity) -> dict:
    """Build the retained discovery config for one entity."""
    payload = {
        "name": entity.name,
        "unique_id": f"{cfg.node_id}_{entity.key}",
        "object_id": f"{cfg.node_id}_{entity.key}",
        "state_topic": cfg.state_topic,
        "value_template": f"{{{{ value_json.{entity.key} }}}}",
        "availability_topic": cfg.availability_topic,
        "payload_available": "online",
        "payload_not_available": "offline",
        "device": device_block(cfg),
    }
    if entity.unit:
        payload["unit_of_measurement"] = entity.unit
    if entity.device_class:
        payload["device_class"] = entity.device_class
    if entity.state_class:
        payload["state_class"] = entity.state_class
    if entity.entity_category:
        payload["entity_category"] = entity.entity_category
    if entity.icon:
        payload["icon"] = entity.icon
    return payload


def build_state(readings: dict) -> dict:
    """Build the state JSON blob, including only fields we actually read.

    A source that returned None is omitted; HA holds the entity's previous value
    (or shows it unknown) rather than being handed a null.
    """
    return {
        e.key: readings[e.key]
        for e in ENTITIES
        if readings.get(e.key) is not None
    }
