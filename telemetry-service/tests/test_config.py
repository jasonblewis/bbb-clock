import configparser
import textwrap

import pytest

from telemetry_service import config


def _parser(text):
    p = configparser.ConfigParser()
    p.read_string(textwrap.dedent(text))
    return p


def test_minimal_config_uses_defaults():
    cfg = config.from_parser(_parser("""
        [mqtt]
        host = newmb.bongalong.st
        username = bbb-clock
        password = s3cret
    """))
    assert cfg.host == "newmb.bongalong.st"
    assert cfg.port == 1883
    assert cfg.base_topic == "bbb-clock"
    assert cfg.discovery_prefix == "homeassistant"
    assert cfg.interval_s == 5.0
    assert cfg.grayscale_max == 4095
    assert cfg.state_topic == "bbb-clock/state"
    assert cfg.availability_topic == "bbb-clock/status"


def test_overrides_are_honoured():
    cfg = config.from_parser(_parser("""
        [mqtt]
        host = broker.local
        port = 8883
        username = u
        password = p
        base_topic = clockroom
        node_id = clockroom

        [telemetry]
        interval_s = 2.5
        tsl2561_port = 5001
        grayscale_max = 1000
        wifi_interface = wlan0
    """))
    assert cfg.port == 8883
    assert cfg.base_topic == "clockroom"
    assert cfg.state_topic == "clockroom/state"
    assert cfg.interval_s == 2.5
    assert cfg.tsl2561_port == 5001
    assert cfg.grayscale_max == 1000
    assert cfg.wifi_interface == "wlan0"


@pytest.mark.parametrize("missing", ["host", "username", "password"])
def test_missing_required_field_raises(missing):
    fields = {"host": "h", "username": "u", "password": "p"}
    del fields[missing]
    body = "[mqtt]\n" + "".join(f"{k} = {v}\n" for k, v in fields.items())
    with pytest.raises(config.ConfigError):
        config.from_parser(_parser(body))


def test_load_missing_file_raises(tmp_path):
    with pytest.raises(config.ConfigError):
        config.load(str(tmp_path / "does-not-exist.conf"))


def test_load_reads_example_shape(tmp_path):
    path = tmp_path / "telemetry.conf"
    path.write_text(textwrap.dedent("""
        [mqtt]
        host = h
        username = u
        password = p
    """))
    cfg = config.load(str(path))
    assert cfg.host == "h"
