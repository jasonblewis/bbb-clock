"""Readers for the three telemetry sources: the lux daemon (:5000), the clock's
/run/clock/brightness file, and Linux /proc + /sys.

Every reader is defensive: it returns None (and never raises) when its source is
unavailable, so one missing source never stops a publish cycle. The pure parsers
are split from the IO so they can be unit-tested without the device.
"""

from __future__ import annotations

import glob
import re
import socket
import subprocess

# tsl2561-daemon serves a single line per TCP connection, e.g.:
#   RC: 0(Success), broadband: 1234, ir: 56, lux: 42
_LUX_RE = re.compile(r"lux:\s*(-?\d+)")


def parse_lux(response: str):
    """Extract the integer lux value from a tsl2561-daemon response line.

    Returns None if the line has no lux field.
    """
    m = _LUX_RE.search(response)
    return int(m.group(1)) if m else None


def read_lux(host: str, port: int, timeout: float = 1.0):
    """Open a connection to tsl2561-daemon, read one reading, return lux (or None)."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            data = sock.recv(1024).decode("ascii", "replace")
        return parse_lux(data)
    except OSError:
        return None


def read_brightness_grayscale(path: str):
    """Read the 0..grayscale_max level clock.c wrote to /run/clock/brightness."""
    try:
        with open(path, "r") as f:
            return int(f.read().strip())
    except (OSError, ValueError):
        return None


def grayscale_to_percent(level, grayscale_max: int = 4095):
    """Convert a 0..grayscale_max display level to an integer 0..100 percent."""
    if level is None or grayscale_max <= 0:
        return None
    pct = round(level / grayscale_max * 100)
    return max(0, min(100, pct))


def read_uptime_s(path: str = "/proc/uptime"):
    """Return system uptime in whole seconds from /proc/uptime (or None)."""
    try:
        with open(path, "r") as f:
            return int(float(f.read().split()[0]))
    except (OSError, ValueError, IndexError):
        return None


def read_load1(path: str = "/proc/loadavg"):
    """Return the 1-minute load average from /proc/loadavg (or None)."""
    try:
        with open(path, "r") as f:
            return float(f.read().split()[0])
    except (OSError, ValueError, IndexError):
        return None


def read_mem_free_kb(path: str = "/proc/meminfo"):
    """Return MemAvailable in kB from /proc/meminfo (or None).

    MemAvailable is the honest "free for new work" figure (falls back to MemFree
    if the kernel is too old to expose it).
    """
    try:
        with open(path, "r") as f:
            fields = {}
            for line in f:
                parts = line.split()
                if len(parts) >= 2 and parts[0].endswith(":"):
                    fields[parts[0][:-1]] = int(parts[1])
    except (OSError, ValueError):
        return None
    return fields.get("MemAvailable", fields.get("MemFree"))


def read_cpu_temp(pattern: str = "/sys/class/thermal/thermal_zone*/temp"):
    """Return CPU temperature in °C from the first thermal zone (or None).

    The AM335x may not expose a thermal zone; drop the entity if absent (per the
    PRD). Values in sysfs are millidegrees C.
    """
    for path in sorted(glob.glob(pattern)):
        try:
            with open(path, "r") as f:
                milli = int(f.read().strip())
            return round(milli / 1000.0, 1)
        except (OSError, ValueError):
            continue
    return None


def _wireless_line(text: str, interface: str = ""):
    """Return the /proc/net/wireless data line for `interface` (or the first
    wireless interface if not specified). None if there is no wireless line.
    """
    for line in text.splitlines():
        line = line.strip()
        if ":" not in line:
            continue
        name, _, rest = line.partition(":")
        name = name.strip()
        # header rows ("Inter-", "face") contain no interface stats
        if not rest.strip() or name in ("Inter-", "face"):
            continue
        if interface and name != interface:
            continue
        return name, rest
    return None


def parse_wifi_rssi(text: str, interface: str = ""):
    """Parse RSSI (dBm) from /proc/net/wireless content.

    Columns: status  link  level  noise ...  — `level` is the signal in dBm
    (often written with a trailing '.'). Returns None if unavailable.
    """
    found = _wireless_line(text, interface)
    if not found:
        return None
    _, rest = found
    cols = rest.split()
    if len(cols) < 3:
        return None
    try:
        return int(float(cols[2].rstrip(".")))
    except ValueError:
        return None


def read_wifi_rssi(path: str = "/proc/net/wireless", interface: str = ""):
    """Read the current WiFi RSSI in dBm from /proc/net/wireless (or None)."""
    try:
        with open(path, "r") as f:
            return parse_wifi_rssi(f.read(), interface)
    except OSError:
        return None


def detect_wifi_interface(path: str = "/proc/net/wireless"):
    """Return the name of the first wireless interface (or None)."""
    try:
        with open(path, "r") as f:
            found = _wireless_line(f.read())
    except OSError:
        return None
    return found[0] if found else None


def read_ssid(interface: str = ""):
    """Best-effort SSID via `iwgetid -r`. Returns None if the tool is missing,
    times out, or the interface is not associated. SSID is a nice-to-have; its
    absence never affects the rest of a publish cycle.
    """
    cmd = ["iwgetid", "-r"]
    if interface:
        cmd.insert(1, interface)
    try:
        out = subprocess.run(
            cmd, capture_output=True, text=True, timeout=2
        )
    except (OSError, subprocess.SubprocessError):
        return None
    ssid = out.stdout.strip()
    return ssid or None
