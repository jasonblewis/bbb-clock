# ADR 0003 — Telemetry via a separate MQTT service; `clock.c` exposes state as `/run/clock/` files

- **Status:** Accepted (design). Implementation is **blocked by the image upgrade**
  (issue #6 — needs a systemd image); design agreed in a `/grill-with-docs` session.
- **Date:** 2026-07-07
- **Scope:** issue #8 — Home Assistant observability over MQTT.

## Context

We want the clock's live state (ambient lux, display brightness, basic system health)
visible in Home Assistant as a single MQTT device. The data lives in three places:

- **ambient lux** — inside `tsl2561-daemon`, served per-connection on `:5000`;
- **display brightness** — computed *only* inside `clock.c` (`brightness_map()` over a
  moving average of ambient lux); nothing else knows it;
- **system metrics** — in `/proc` and `/sys`.

Constraints: `clock.c` is the render process and must never miss a frame — we do not want
to grow it or add anything that can stall its loop. The on-device C toolchain is old
(gcc 4.6.3), which makes linking a modern MQTT library into the C processes painful.
Local dimming must keep working if the broker or any telemetry code is down.

## Decision

1. **A separate telemetry service** (Python 3 + `paho-mqtt`, supervised by systemd) owns
   all MQTT: connection, reconnect, Last-Will availability, Home Assistant discovery, and
   publishing. `tsl2561-daemon` is unchanged; `clock.c` stays barebones.
2. **`clock.c` exposes its brightness by writing `/run/clock/brightness`** (tmpfs) whenever
   the value changes — a ~1-line addition, no server, no protocol. This generalises to a
   convention: **`clock.c` publishes internal state as one-value-per-file under
   `/run/clock/`**; future internals (e.g. glitch counts) just add a file.
3. The service *gathers* the three sources (poll `:5000`, read `/run/clock/*`, read
   `/proc`+`/sys`) and publishes one JSON state blob every ~5 s.

## Alternatives considered

- **Build MQTT into `clock.c` / the C processes.** Rejected: adds bug surface and blocking
  risk to the one process that must render every frame, and forces a C MQTT client onto
  gcc 4.6.3. (Initially chosen in discussion, then reversed — "that is bugs waiting to
  happen; keep `clock.c` barebones".)
- **A `:5001` socket server inside `clock.c`** (mirroring `tsl2561-daemon`), queried by the
  service. Rejected: makes the render process a server, needing a non-blocking listen +
  `accept()` polled in the render loop (~15–20 lines and real timing care) to get a value
  the file gives for ~1 line.
- **Process signals** (service signals `clock.c`, which replies). Rejected: plain signals
  carry no payload and real-time signals carry only a single `int`; the direction is
  wrong for a query; handlers are async-signal-safe-constrained; and it couples the two
  processes by PID. Not less code, and more ways to get subtly wrong.
- **Service re-derives brightness from lux** (replicate the moving average + mapping).
  Rejected: duplicates `clock.c` logic, silently drifts if the mapping changes, and
  reports a *guess* rather than the display's real setting.

The `/run/clock/` file wins on all axes that mattered: least code, faithful to the real
value, no timing risk to rendering, and trivially extensible.

## Consequences

- `clock.c`'s footprint for this feature is one file write; the render loop is untouched.
- `/run` is tmpfs, so brightness writes never wear the SD card.
- **Requires systemd** — for the service unit and a `systemd-tmpfiles.d` entry that creates
  `/run/clock/`. Hence the hard dependency on #6 (do the image upgrade first).
- **Local dimming is immune** to broker/service outages: `clock.c` only writes a file, so
  nothing downstream can stall the display.
- *Limitation:* if `clock.c` itself dies, `/run/clock/brightness` holds a stale value the
  service cannot detect (HA availability tracks the *service*, not `clock.c`). A future
  file-mtime freshness check can address it; out of scope for v1.

## Related

- Issue #8 (the PRD). Blocked by issue #6 (image upgrade).
- `CONTEXT.md` — glossary terms **ambient lux** and **display brightness**.
- `tsl2561-daemon/` — the unchanged lux source; `clock.c` `brightness_map()` — the
  brightness source.
