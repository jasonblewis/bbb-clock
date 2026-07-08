# Migration outcome — as-built on Debian 13.5 / kernel 6.18

Issue [#6](https://github.com/jasonblewis/bbb-clock/issues/6). Completed 2026-07-08.
The clock runs on the new image with SPI display, TSL2561 brightness, and both services
under systemd. This is the authoritative record of the *final* config — where it differs
from [`target-image-plan.md`](target-image-plan.md)'s predictions, this doc wins.

## Final configuration

Kernel note: the image shipped **6.18.32-bone35**; the box later took a kernel update to
**6.18.38-bone42** and everything held (SPI overlay, `gpio628`, the 512-based sysfs bases
were all stable across the bump).

| Concern | Old (Wheezy/3.8) | **New (Trixie / kernel 6.18.x)** |
|---|---|---|
| SPI node | `/dev/spidev1.0` | **`/dev/spidev0.0`** (`clock.c`) |
| SPI enable | capemgr `BB-SPI0-01` | **`uboot_overlay_addr0=BB-SPIDEV0-00A0.dtbo`** in `/boot/uEnv.txt` (stock overlay, same P9_17/18/21/22 pins) |
| Sensor I²C | `/dev/i2c-1` | **`/dev/i2c-2`** @ 0x39 → `TSL2561_INIT(2, …)` in `tsl2561-daemon.c` |
| Unblank pin | `gpio20` (P9_41A / gpio0_20), sysfs | **`gpio628` (P9_41B / gpio3_20)**, sysfs — see below |
| Build | gcc 4.6.3 | gcc-14 on device; `-lm` moved after source; daemon built `-std=gnu17` |
| Services | sysvinit `/etc/init.d` + `rc.local` | **systemd**: `tsl2561-daemon.service`, `clock.service` (see `deploy/systemd/`) |
| Boot storage | eMMC present | eMMC disabled (`disable_uboot_overlay_emmc=1`) — leftover, harmless (boots from SD) |

## The unblank pin — the one real surprise

The P1 unblank (`clock.c: unblank_display()`) was the hard part. Findings:

- Header **P9_41 is a BBB "double pin"**: two SoC balls tie to the same header pin —
  **P9_41A = gpio0_20** (pad XDMA_EVENT_INTR1) and **P9_41B = gpio3_20** (pad
  MCASP0_AHCLKR). The old kernel drove P9_41A as sysfs `gpio20`.
- On 6.18, **P9_41A is unusable**: its pad is stuck in **mux mode 3** (not gpio), and
  `gpio0_20` is claimed by the eMMC reset (`[emmc rst]`). Disabling eMMC frees the *line*
  but the pad still isn't muxed to gpio, and the AM335x control-module pad registers are
  **write-protected from `/dev/mem`** (can't re-mux at runtime; no `config-pin` on this
  image, no pinmux-helper `state` node).
- **P9_41B / gpio3_20's pad is already in mux mode 7 (gpio)** — pad `0x44e109a8` reads
  `0x27`. Driving it high drives the same physical header pin. So the fix is simply to
  **drive gpio3_20 instead of gpio0_20** — no overlay, no rewiring.
- **sysfs numbering is 512-based** on this kernel (gpiochip bases 512/544/576/608, not
  0/32/64/96). So `gpio3_20 = 608 + 20 = ` **`628`**. (`echo 20 > …/export` failed with
  EINVAL simply because 20 isn't a valid number here — it was never a "claimed pin".)

`clock.c` drives `/sys/class/gpio/gpio628` (`BLANK_GPIO`). If a future kernel shifts the
gpiochip base again, recompute: `gpiochip3 base` (`cat /sys/class/gpio/gpiochip*/label`
→ the `gpio-96-127` one) `+ 20`. libgpiod (`gpioset -c gpiochip3 20=1`) is the
numbering-independent alternative if this ever drifts.

## Build notes (gcc-14)

- `clock`: `make clock` — moved `-lm` **after** `clock.c` (modern `ld --as-needed` drops a
  library listed before the objects that use it). Otherwise C17-clean.
- `tsl2561-daemon`: build with **`-std=gnu17`** (strict `c17` hides POSIX `usleep` in the
  TSL2561 lib). The daemon links `tsl2561/TSL2561.c` (vendored — see PR #15) and excludes
  `client.c` (which has its own `main`). Makefile updated to do this.

## Verify checklist (on device)

- `/dev/spidev0.0` present, `i2cdetect -y -r 2` shows `0x39`.
- `systemctl is-active clock.service tsl2561-daemon.service` → active/active.
- Display shows time; brightness tracks the sensor; `gpio628` value = 1 while running.
- P1: cold power-cycle → display stays dark through boot, comes up clean (no garbage).
- P2: run `p2stress`.
