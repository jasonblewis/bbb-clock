# TODO

Active work is tracked as **GitHub issues** on `jasonblewis/bbb-clock`
(`gh issue list`), not in this file. The items that used to live here have been
migrated:

| Was | Now |
|---|---|
| Upgrade to a newer BBB / Debian image | [#6](https://github.com/jasonblewis/bbb-clock/issues/6) |
| Reduce SD-card writes (flash wear) | [#7](https://github.com/jasonblewis/bbb-clock/issues/7) |
| Home Assistant observability over MQTT (brightness + telemetry) | [#8](https://github.com/jasonblewis/bbb-clock/issues/8) (PRD) |
| Rename default branch `master` → `main` | [#9](https://github.com/jasonblewis/bbb-clock/issues/9) |
| Speed up the dimming/brightening ramp | [#10](https://github.com/jasonblewis/bbb-clock/issues/10) |
| Only send to the TLC5947 on change, not every refresh | [#11](https://github.com/jasonblewis/bbb-clock/issues/11) |
| Turn off status LEDs ~30s after boot (WiFi + onboard) | [#12](https://github.com/jasonblewis/bbb-clock/issues/12) |

Completed items (display blanking circuit, boot-process documentation and
simplification, driver-board review, and the blank→update→blank check) have been
removed — see the git history and `docs/adr/` for the record. The runtime flicker
(P2) is fixed in hardware; see `docs/adr/0002-p2-ribbon-crosstalk.md`.
