# telemetry-service

Publishes the BBB clock's live state to Home Assistant as a single MQTT device.

This is the implementation of [issue #8](https://github.com/jasonblewis/bbb-clock/issues/8);
the design is [ADR 0003](../docs/adr/0003-telemetry-service-and-run-clock.md).

## What it does

One ~5 s loop gathers three sources and publishes one JSON blob to MQTT:

| Source | Read | Entity |
|---|---|---|
| `tsl2561-daemon` on `127.0.0.1:5000` | one lux reading per connection | Ambient lux (lx) |
| `/run/clock/brightness` | the 0–4095 level `clock.c` is driving | Display brightness (%) |
| `/proc`, `/sys`, `/proc/net/wireless` | uptime, load, free mem, CPU temp, WiFi RSSI, SSID | system health |

MQTT layout (one device, retained discovery, live state, LWT availability):

- **discovery** — `homeassistant/sensor/bbb-clock/<key>/config`, published once, **retained**.
- **state** — one JSON blob on `bbb-clock/state` each cadence, **not retained**.
- **availability** — `bbb-clock/status` = `online`/`offline`, **retained**; Last-Will
  flips it to `offline` if the service or broker drops.

**Guarantees:** the clock display is never affected by this service; a broker outage
just drops publishes (paho auto-reconnects); a missing source publishes the fields it
did get and retries next cycle.

## Develop / test (off-device)

```sh
cd telemetry-service
python3 -m venv .venv && .venv/bin/pip install -e '.[]' pytest
.venv/bin/pytest            # sources/discovery/config/service logic, no broker needed
```

The pure logic (parsers, discovery payloads, state building) is fully unit-tested with
mock files and a fake MQTT client — no device, broker, or Home Assistant required.

## Deploy on the device

Prerequisites: a **systemd** image (issue #6, done), `/run/clock/` created by the
tmpfiles entry, and `clock.c` writing `/run/clock/brightness` (both in this repo).

1. **Create the broker user.** On `newmb.bongalong.st`, add a dedicated `bbb-clock`
   Mosquitto user/password (not an admin account). Keep the password out of git.

2. **Install the service (venv sidesteps Debian 13 PEP-668):**
   ```sh
   sudo python3 -m venv /usr/local/lib/bbb-clock/venv
   sudo /usr/local/lib/bbb-clock/venv/bin/pip install /path/to/telemetry-service
   ```

3. **Config (holds the password — mode 600):**
   ```sh
   sudo install -d /etc/bbb-clock
   sudo install -m 600 telemetry.conf.example /etc/bbb-clock/telemetry.conf
   sudoedit /etc/bbb-clock/telemetry.conf     # set the broker password
   ```

4. **tmpfiles + units:**
   ```sh
   sudo install -m 644 deploy/tmpfiles.d/bbb-clock.conf /etc/tmpfiles.d/bbb-clock.conf
   sudo systemd-tmpfiles --create /etc/tmpfiles.d/bbb-clock.conf     # makes /run/clock now
   sudo install -m 644 deploy/systemd/telemetry-service.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now telemetry-service.service
   ```

## Verify

- `systemctl is-active telemetry-service` → `active`; `journalctl -u telemetry-service -f`.
- `mosquitto_sub -h newmb.bongalong.st -u bbb-clock -P … -t 'bbb-clock/#' -v` shows a
  `bbb-clock/state` JSON blob every ~5 s and `bbb-clock/status online`.
- In Home Assistant: one **BBB Clock** device with the v1 entities, updating live.
- `sudo systemctl stop telemetry-service` → device goes **unavailable** in HA (LWT).
- Stopping the broker or this service leaves the **clock display + dimming unaffected**.
