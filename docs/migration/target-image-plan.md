# Migration plan — target image (Debian 13.5 / kernel 6.18)

Companion to [`current-image-baseline.md`](current-image-baseline.md). Findings below were
verified **2026-07-08** by loop-mounting the downloaded image
(`am335x-debian-13.5-base-v6.18-armhf-2026-05-19-4gb.img.xz`, sha256 **OK**) and reading
its boot config, kernel config, overlays, and package set directly. Issue
[#6](https://github.com/jasonblewis/bbb-clock/issues/6).

## Target image — confirmed facts

| | Old (Wheezy) | **New (target)** |
|---|---|---|
| Distro | Debian 7.11 | **Debian 13.5 "Trixie" Base** (2026-05-19) |
| Kernel | 3.8.13-bone41 | **6.18.32-bone35** |
| Init | sysvinit | **systemd** (present) |
| Compiler | gcc 4.6.3 | **gcc-14** (`gcc`,`cc`,`make` all in base image) |
| Overlays | capemgr slots | **u-boot overlays** via `/boot/uEnv.txt` (`enable_uboot_overlays=1`) |
| SPI enable | custom `BB-SPI0-01` | **stock `BB-SPIDEV0-00A0.dtbo`** ships in image |
| GPIO sysfs | `/sys/class/gpio` (legacy) | **still present** — `CONFIG_GPIO_SYSFS=y` + `CONFIG_GPIO_SYSFS_LEGACY=y` |
| spidev | module | `CONFIG_SPI_SPIDEV=m` |
| libgpiod | n/a | **v3 + `gpioset`/`gpioget`/`gpioinfo` CLI** present (fallback) |

**Partition layout of the image:** p1 FAT32 36 MB (`/boot/firmware`, mostly the config
web-UI, *not* kernel/overlays), p2 512 MB swap, p3 3 GB ext4 root. Kernel, dtbs and
overlays live in the **rootfs** `/boot/`. Image is pre-sized for a 4 GB card ✓.

## What each old assumption becomes

- **Build-on-device stays.** gcc-14 + make ship in the base image — no cross-compile
  needed. (clock.c was already made C17-clean per `CLAUDE.md`; still expect gcc-14 to be
  far stricter than 4.6.3 — implicit declarations are now hard errors. Rebuild + fix
  warnings is a verification step.)
- **Custom `BB-SPI0-01` overlay retired.** Use stock **`BB-SPIDEV0-00A0`**. Its source
  (`/opt/source/.../overlays/BB-SPIDEV0-00A0.dtso`) enables SPI0 on the **same physical
  pins** the clock already uses: **P9_17 (CS0), P9_18 (D1/MOSI), P9_21 (D0), P9_22
  (SCLK)**. The repo's `BB-SPI0-01-00A0.dts/.dtbo` and `BB-BLANK-00A0.dts` are no longer
  needed on-device.
- **P1 gpio20 unblank likely ports unchanged.** `CONFIG_GPIO_SYSFS_LEGACY=y` keeps the old
  global `/sys/class/gpio/gpioNN` numbering, so `clock.c`'s `unblank_display()` may work
  verbatim. **Must verify on device** that `gpio20` still maps to the blank pin
  (`gpioinfo` / export test). If numbering shifted, port to libgpiod v3 (present) — chip +
  line, not a global number.

## Concrete change list

1. **Enable SPI0** — in `/boot/uEnv.txt`, uncomment/set:
   `uboot_overlay_addr0=BB-SPIDEV0-00A0.dtbo` (u-boot resolves it from the overlays dir).
   Reboot; confirm **`/dev/spidev0.0`** appears.
2. **`clock.c` SPI node** — change `/dev/spidev1.0` → **`/dev/spidev0.0`** (old TI kernel
   numbered McSPI0 as bus 1; mainline numbers it bus 0). This is the one certain code
   change. `clock.c:278`.
3. **SPI mode check** — the stock overlay sets `spi-cpha` (→ mode 1) on CS0, and `clock.c`
   has `SPI_IOC_WR_MODE` **commented out** (it only *reads* mode), so it inherits the DT
   mode. Verify the display latches correctly under mode 1; if not, set the mode
   explicitly. (Compare against whatever the old `BB-SPI0-01` implied.)
4. **gpio20 unblank** — expected to work as-is (see above); verify, else libgpiod.
5. **Two systemd units** replacing `/etc/init.d/*` + `rc.local`:
   - `tsl2561-daemon.service` (light sensor; starts first)
   - `clock.service` → `ExecStart=/usr/local/bin/clock -t`, `After=tsl2561-daemon.service`
     and ordered after SPI/`/dev/spidev0.0` is present.
   - **No early-unblank unit** — preserve P1 fail-safe (hardware pull-down + clock's
     self-unblank after first frame). Do not recreate the old `displayon` service.
6. **Deploy binaries** — rebuild `clock` from `clock.c` on-device; bring over
   `tsl2561-daemon`; install to `/usr/local/bin`.
7. **Re-verify** — P1 cold-boot (display stays dark through boot, no garbage flash; ADR
   0001) and P2 `p2stress` sweep (ADR 0002).

## Rollback

Flash the new image to a **separate** SD card. Keep the **current card untouched** as a
drop-in rollback. Confirm the user's `tar | bzip2` backup before flashing.

## Open verification items (can only be answered on the booted new image)

- gpio20 legacy number still maps to the blank pin.
- SPI mode 1 vs the display chip's requirement.
- gcc-14 rebuild of `clock.c` is warning/error clean.
- `tsl2561-daemon` still builds/runs (its own source/toolchain deps under gcc-14).
