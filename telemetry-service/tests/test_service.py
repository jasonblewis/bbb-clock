import json

from telemetry_service import service
from telemetry_service.config import Config


class FakeClient:
    """Records the MQTT calls the service makes, so we can assert on them
    without a broker or paho."""

    def __init__(self):
        self.published = []       # (topic, payload, qos, retain)
        self.will = None
        self.on_connect = None
        self.on_disconnect = None
        self.reconnect_delay = None

    def username_pw_set(self, u, p):
        self.creds = (u, p)

    def will_set(self, topic, payload, qos=0, retain=False):
        self.will = (topic, payload, qos, retain)

    def reconnect_delay_set(self, **kw):
        self.reconnect_delay = kw

    def publish(self, topic, payload=None, qos=0, retain=False):
        self.published.append((topic, payload, qos, retain))

    # exercised only via run(), not in these unit tests
    def connect_async(self, *a, **k):
        pass

    def loop_start(self):
        pass

    def loop_stop(self):
        pass

    def disconnect(self):
        pass


CFG = Config(host="h", username="u", password="p")


def make_service():
    fake = FakeClient()
    svc = service.TelemetryService(CFG, client=fake)
    return svc, fake


def test_last_will_is_offline_retained():
    _, fake = make_service()
    assert fake.will == ("bbb-clock/status", "offline", 1, True)


def test_credentials_set_from_config():
    _, fake = make_service()
    assert fake.creds == ("u", "p")


def test_on_connect_publishes_discovery_and_online():
    svc, fake = make_service()
    svc._on_connect(fake, None, None, 0)

    discovery_msgs = [m for m in fake.published if m[0].startswith("homeassistant/")]
    assert len(discovery_msgs) == len(service.ENTITIES)
    # discovery is retained
    assert all(retain for (_t, _p, _q, retain) in discovery_msgs)
    # each discovery payload is valid JSON carrying the shared device block
    for topic, payload, _q, _r in discovery_msgs:
        obj = json.loads(payload)
        assert obj["device"]["identifiers"] == ["bbb-clock"]

    online = [m for m in fake.published if m == ("bbb-clock/status", "online", 1, True)]
    assert online, "availability 'online' (retained) must be published on connect"


def test_on_connect_failure_publishes_nothing():
    svc, fake = make_service()
    svc._on_connect(fake, None, None, 5)  # non-zero reason code = failure
    assert fake.published == []


def test_publish_once_emits_state_blob(monkeypatch):
    svc, fake = make_service()
    monkeypatch.setattr(service, "gather_readings", lambda cfg: {
        "lux": 42, "brightness": 71, "uptime_s": 90210, "cpu_temp": None,
    })
    state = svc.publish_once()

    assert state == {"lux": 42, "brightness": 71, "uptime_s": 90210}
    topic, payload, qos, retain = fake.published[-1]
    assert topic == "bbb-clock/state"
    assert retain is False           # state must NOT be retained
    assert json.loads(payload) == state


def test_gather_readings_maps_sources(monkeypatch):
    monkeypatch.setattr(service.sources, "read_lux", lambda h, p: 42)
    monkeypatch.setattr(service.sources, "read_brightness_grayscale", lambda p: 2048)
    monkeypatch.setattr(service.sources, "read_uptime_s", lambda: 100)
    monkeypatch.setattr(service.sources, "read_cpu_temp", lambda: 48.2)
    monkeypatch.setattr(service.sources, "detect_wifi_interface", lambda: "wlan0")
    monkeypatch.setattr(service.sources, "read_wifi_rssi", lambda interface="": -58)
    monkeypatch.setattr(service.sources, "read_ssid", lambda iface: "bongalong")
    monkeypatch.setattr(service.sources, "read_load1", lambda: 0.07)
    monkeypatch.setattr(service.sources, "read_mem_free_kb", lambda: 812345)

    r = service.gather_readings(CFG)
    assert r["lux"] == 42
    assert r["brightness"] == 50          # 2048/4095 -> 50%
    assert r["cpu_temp"] == 48.2
    assert r["ssid"] == "bongalong"


def test_stop_sets_event():
    svc, _ = make_service()
    assert not svc._stop.is_set()
    svc.stop()
    assert svc._stop.is_set()
