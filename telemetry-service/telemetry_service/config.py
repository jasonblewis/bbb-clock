"""Configuration loading for the telemetry service.

Config lives in a service-local INI file on the device (default
/etc/bbb-clock/telemetry.conf), NOT in the repo, because it holds the MQTT
password. See telemetry.conf.example for the shape.
"""

from __future__ import annotations

import configparser
from dataclasses import dataclass

DEFAULT_CONFIG_PATH = "/etc/bbb-clock/telemetry.conf"


class ConfigError(Exception):
    """Raised when the config file is missing required fields."""


@dataclass(frozen=True)
class Config:
    # MQTT broker / auth
    host: str
    username: str
    password: str
    port: int = 1883
    # MQTT topic layout
    base_topic: str = "bbb-clock"
    discovery_prefix: str = "homeassistant"
    node_id: str = "bbb-clock"
    # Telemetry sources / cadence
    interval_s: float = 5.0
    tsl2561_host: str = "127.0.0.1"
    tsl2561_port: int = 5000
    brightness_path: str = "/run/clock/brightness"
    grayscale_max: int = 4095
    # Empty string => auto-detect the first wireless interface.
    wifi_interface: str = ""

    @property
    def state_topic(self) -> str:
        return f"{self.base_topic}/state"

    @property
    def availability_topic(self) -> str:
        return f"{self.base_topic}/status"


def load(path: str = DEFAULT_CONFIG_PATH) -> Config:
    """Load and validate config from an INI file.

    Raises ConfigError if the file cannot be read or a required MQTT field
    (host/username/password) is missing.
    """
    parser = configparser.ConfigParser()
    read = parser.read(path)
    if not read:
        raise ConfigError(f"config file not found or unreadable: {path}")
    return from_parser(parser)


def from_parser(parser: configparser.ConfigParser) -> Config:
    """Build a Config from an already-parsed INI (split out for testing)."""
    mqtt = parser["mqtt"] if parser.has_section("mqtt") else {}
    tel = parser["telemetry"] if parser.has_section("telemetry") else {}

    def req(section, key):
        val = section.get(key, "").strip() if hasattr(section, "get") else ""
        if not val:
            raise ConfigError(f"[mqtt] {key} is required")
        return val

    defaults = Config(host="", username="", password="")
    return Config(
        host=req(mqtt, "host"),
        username=req(mqtt, "username"),
        password=req(mqtt, "password"),
        port=int(mqtt.get("port", defaults.port)),
        base_topic=mqtt.get("base_topic", defaults.base_topic).strip() or defaults.base_topic,
        discovery_prefix=mqtt.get("discovery_prefix", defaults.discovery_prefix).strip()
        or defaults.discovery_prefix,
        node_id=mqtt.get("node_id", defaults.node_id).strip() or defaults.node_id,
        interval_s=float(tel.get("interval_s", defaults.interval_s)),
        tsl2561_host=tel.get("tsl2561_host", defaults.tsl2561_host).strip()
        or defaults.tsl2561_host,
        tsl2561_port=int(tel.get("tsl2561_port", defaults.tsl2561_port)),
        brightness_path=tel.get("brightness_path", defaults.brightness_path).strip()
        or defaults.brightness_path,
        grayscale_max=int(tel.get("grayscale_max", defaults.grayscale_max)),
        wifi_interface=tel.get("wifi_interface", defaults.wifi_interface).strip(),
    )
