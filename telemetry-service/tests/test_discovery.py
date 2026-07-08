from telemetry_service import discovery
from telemetry_service.config import Config

CFG = Config(host="h", username="u", password="p")


def _entity(key):
    return next(e for e in discovery.ENTITIES if e.key == key)


def test_every_entity_key_is_unique():
    keys = [e.key for e in discovery.ENTITIES]
    assert len(keys) == len(set(keys))


def test_prd_v1_entities_present():
    keys = {e.key for e in discovery.ENTITIES}
    assert {
        "lux", "brightness", "uptime_s", "cpu_temp",
        "wifi_rssi", "ssid", "load1", "mem_free_kb",
    } <= keys


def test_discovery_topic_layout():
    topic = discovery.discovery_topic(CFG, _entity("lux"))
    assert topic == "homeassistant/sensor/bbb-clock/lux/config"


def test_discovery_payload_core_fields():
    payload = discovery.discovery_payload(CFG, _entity("lux"))
    assert payload["unique_id"] == "bbb-clock_lux"
    assert payload["state_topic"] == "bbb-clock/state"
    assert payload["value_template"] == "{{ value_json.lux }}"
    assert payload["availability_topic"] == "bbb-clock/status"
    assert payload["payload_available"] == "online"
    assert payload["payload_not_available"] == "offline"
    assert payload["unit_of_measurement"] == "lx"
    assert payload["device_class"] == "illuminance"


def test_all_entities_share_one_device_identifier():
    ids = {
        tuple(discovery.discovery_payload(CFG, e)["device"]["identifiers"])
        for e in discovery.ENTITIES
    }
    assert ids == {("bbb-clock",)}


def test_brightness_is_percent_without_device_class():
    payload = discovery.discovery_payload(CFG, _entity("brightness"))
    assert payload["unit_of_measurement"] == "%"
    assert "device_class" not in payload


def test_ssid_is_plain_text_sensor():
    payload = discovery.discovery_payload(CFG, _entity("ssid"))
    assert "unit_of_measurement" not in payload
    assert "state_class" not in payload
    assert payload["entity_category"] == "diagnostic"


def test_value_template_matches_state_field_for_every_entity():
    # The whole design relies on entity.key == state-JSON field.
    readings = {e.key: 1 for e in discovery.ENTITIES}
    state = discovery.build_state(readings)
    for e in discovery.ENTITIES:
        payload = discovery.discovery_payload(CFG, e)
        assert payload["value_template"] == f"{{{{ value_json.{e.key} }}}}"
        assert e.key in state


def test_build_state_omits_missing_sources():
    readings = {"lux": 42, "brightness": 71, "cpu_temp": None}
    state = discovery.build_state(readings)
    assert state == {"lux": 42, "brightness": 71}
    assert "cpu_temp" not in state


def test_build_state_keeps_zero_values():
    # 0 lux / 0% brightness are real readings, not "missing".
    state = discovery.build_state({"lux": 0, "brightness": 0})
    assert state == {"lux": 0, "brightness": 0}
