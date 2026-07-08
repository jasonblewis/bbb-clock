"""The telemetry loop: gather the three sources, publish one JSON state blob to
MQTT every cadence, with retained HA discovery and a Last-Will availability topic.

Design guarantees (issue #8 / ADR 0003):
- The clock display is never affected by anything here.
- A broker outage never crashes the service: paho auto-reconnects with backoff
  and publishes are simply dropped while offline.
- A source being unavailable degrades gracefully: publish the fields we did get.
"""

from __future__ import annotations

import json
import logging
import signal
import threading

from . import sources
from .discovery import ENTITIES, build_state, discovery_payload, discovery_topic

log = logging.getLogger("telemetry")


def gather_readings(cfg) -> dict:
    """Read every source once. Missing sources come back as None (never raise)."""
    iface = cfg.wifi_interface or sources.detect_wifi_interface() or ""
    level = sources.read_brightness_grayscale(cfg.brightness_path)
    return {
        "lux": sources.read_lux(cfg.tsl2561_host, cfg.tsl2561_port),
        "brightness": sources.grayscale_to_percent(level, cfg.grayscale_max),
        "uptime_s": sources.read_uptime_s(),
        "cpu_temp": sources.read_cpu_temp(),
        "wifi_rssi": sources.read_wifi_rssi(interface=iface),
        "ssid": sources.read_ssid(iface),
        "load1": sources.read_load1(),
        "mem_free_kb": sources.read_mem_free_kb(),
    }


class TelemetryService:
    def __init__(self, cfg, client=None):
        self.cfg = cfg
        self._stop = threading.Event()
        self._client = client if client is not None else self._make_client()
        self._configure_client()

    # --- MQTT setup -------------------------------------------------------
    def _make_client(self):
        # Imported here so the pure logic (and its tests) never needs paho.
        import paho.mqtt.client as mqtt

        return mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=self.cfg.node_id,
        )

    def _configure_client(self):
        c = self.cfg
        self._client.username_pw_set(c.username, c.password)
        # Last-Will: broker marks us offline (retained) if we drop unexpectedly.
        self._client.will_set(c.availability_topic, "offline", qos=1, retain=True)
        self._client.reconnect_delay_set(min_delay=1, max_delay=60)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code != 0:
            log.warning("MQTT connect failed: %s", reason_code)
            return
        log.info("MQTT connected; publishing discovery + availability")
        self._publish_discovery()
        client.publish(self.cfg.availability_topic, "online", qos=1, retain=True)

    def _on_disconnect(self, client, userdata, *args):
        # paho handles reconnect; just note it. (Signature varies across paho
        # versions, so accept extra args.)
        log.warning("MQTT disconnected; paho will reconnect")

    def _publish_discovery(self):
        for entity in ENTITIES:
            self._client.publish(
                discovery_topic(self.cfg, entity),
                json.dumps(discovery_payload(self.cfg, entity)),
                qos=1,
                retain=True,
            )

    # --- run loop ---------------------------------------------------------
    def publish_once(self):
        readings = gather_readings(self.cfg)
        state = build_state(readings)
        self._client.publish(
            self.cfg.state_topic, json.dumps(state), qos=0, retain=False
        )
        log.debug("published state: %s", state)
        return state

    def run(self):
        c = self.cfg
        self._install_signal_handlers()
        self._client.connect_async(c.host, c.port)
        self._client.loop_start()
        log.info("telemetry loop started (every %.1fs)", c.interval_s)
        try:
            while not self._stop.is_set():
                try:
                    self.publish_once()
                except Exception:  # never let one bad cycle kill the loop
                    log.exception("publish cycle failed; continuing")
                self._stop.wait(c.interval_s)
        finally:
            self._shutdown()

    def stop(self, *args):
        self._stop.set()

    def _install_signal_handlers(self):
        for sig in (signal.SIGTERM, signal.SIGINT):
            signal.signal(sig, self.stop)

    def _shutdown(self):
        log.info("shutting down; marking offline")
        try:
            self._client.publish(
                self.cfg.availability_topic, "offline", qos=1, retain=True
            )
            self._client.loop_stop()
            self._client.disconnect()
        except Exception:
            log.exception("error during shutdown")
