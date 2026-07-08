"""BBB Clock telemetry service.

Gathers the clock's live state (ambient lux, display brightness, basic system
health) and publishes it to Home Assistant as a single MQTT device. See issue #8
and docs/adr/0003-telemetry-service-and-run-clock.md.

The clock render process (clock.c) and tsl2561-daemon stay barebones; this
service only *reads* their state and owns everything MQTT.
"""

__version__ = "0.1.0"
