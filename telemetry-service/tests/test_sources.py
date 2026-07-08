import textwrap

from telemetry_service import sources


# --- lux parsing ---------------------------------------------------------
def test_parse_lux_typical_daemon_line():
    line = "RC: 0(Success), broadband: 1234, ir: 56, lux: 42"
    assert sources.parse_lux(line) == 42


def test_parse_lux_zero_and_negative():
    assert sources.parse_lux("... lux: 0") == 0
    assert sources.parse_lux("... lux: -1") == -1


def test_parse_lux_missing_field():
    assert sources.parse_lux("RC: 5(Error), no reading") is None


# --- grayscale -> percent ------------------------------------------------
def test_grayscale_to_percent_bounds():
    assert sources.grayscale_to_percent(0) == 0
    assert sources.grayscale_to_percent(4095) == 100
    assert sources.grayscale_to_percent(2048) == 50


def test_grayscale_to_percent_none_passthrough():
    assert sources.grayscale_to_percent(None) is None


def test_grayscale_to_percent_clamps_out_of_range():
    assert sources.grayscale_to_percent(9000) == 100
    assert sources.grayscale_to_percent(-5) == 0


# --- brightness file -----------------------------------------------------
def test_read_brightness_grayscale(tmp_path):
    f = tmp_path / "brightness"
    f.write_text("1234\n")
    assert sources.read_brightness_grayscale(str(f)) == 1234


def test_read_brightness_missing_file(tmp_path):
    assert sources.read_brightness_grayscale(str(tmp_path / "nope")) is None


def test_read_brightness_garbage(tmp_path):
    f = tmp_path / "brightness"
    f.write_text("not-a-number")
    assert sources.read_brightness_grayscale(str(f)) is None


# --- /proc readers -------------------------------------------------------
def test_read_uptime_s(tmp_path):
    f = tmp_path / "uptime"
    f.write_text("90210.42 812345.11\n")
    assert sources.read_uptime_s(str(f)) == 90210


def test_read_load1(tmp_path):
    f = tmp_path / "loadavg"
    f.write_text("0.07 0.10 0.09 1/123 4567\n")
    assert sources.read_load1(str(f)) == 0.07


def test_read_mem_free_prefers_memavailable(tmp_path):
    f = tmp_path / "meminfo"
    f.write_text(textwrap.dedent("""\
        MemTotal:        1000000 kB
        MemFree:          200000 kB
        MemAvailable:     812345 kB
        Buffers:           10000 kB
    """))
    assert sources.read_mem_free_kb(str(f)) == 812345


def test_read_mem_free_falls_back_to_memfree(tmp_path):
    f = tmp_path / "meminfo"
    f.write_text("MemTotal: 1000000 kB\nMemFree: 200000 kB\n")
    assert sources.read_mem_free_kb(str(f)) == 200000


# --- /sys thermal --------------------------------------------------------
def test_read_cpu_temp(tmp_path):
    zone = tmp_path / "thermal_zone0"
    zone.mkdir()
    (zone / "temp").write_text("48200\n")
    assert sources.read_cpu_temp(str(tmp_path / "thermal_zone*/temp")) == 48.2


def test_read_cpu_temp_absent(tmp_path):
    # AM335x may expose no thermal zone -> None (entity dropped).
    assert sources.read_cpu_temp(str(tmp_path / "thermal_zone*/temp")) is None


# --- /proc/net/wireless --------------------------------------------------
WIRELESS = textwrap.dedent("""\
    Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE
     face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22
      wlan0: 0000   58.  -58.  -256        0      0      0      0     12        0
""")


def test_parse_wifi_rssi():
    assert sources.parse_wifi_rssi(WIRELESS) == -58


def test_parse_wifi_rssi_specific_interface():
    assert sources.parse_wifi_rssi(WIRELESS, interface="wlan0") == -58
    assert sources.parse_wifi_rssi(WIRELESS, interface="wlan9") is None


def test_detect_wifi_interface(tmp_path):
    f = tmp_path / "wireless"
    f.write_text(WIRELESS)
    assert sources.detect_wifi_interface(str(f)) == "wlan0"


def test_parse_wifi_rssi_no_interfaces():
    header_only = "\n".join(WIRELESS.splitlines()[:2]) + "\n"
    assert sources.parse_wifi_rssi(header_only) is None
