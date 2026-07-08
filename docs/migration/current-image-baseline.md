# Migration baseline — current BBB image (pre-migration snapshot)

Captured **2026-07-08** from the live device (`root@clock.local`) as the reference point
for issue [#6](https://github.com/jasonblewis/bbb-clock/issues/6) (upgrade to a newer
BeagleBoard Debian image). This is *what we are migrating away from*; keep the old SD card
intact as instant rollback.

## Image / toolchain

| | Current (to be replaced) |
|---|---|
| Distro | Debian **7.11 "Wheezy"** |
| Kernel | **3.8.13-bone41** (2014) |
| Init | **sysvinit** (pre-systemd) — `pid 1 = init` |
| Overlays | **capemgr** era (`/sys/devices/bone_capemgr.*/slots`) |
| Compiler | gcc **4.6.3** (build-on-device) |

The **capemgr → u-boot `uEnv.txt` overlay** change and the **sysvinit → systemd** change
are the two structural jumps. Everything below is expressed in old-image terms and must be
re-expressed for the target image.

## Disk (fits a 4 GB card?)

- `/dev/mmcblk0p2` (rootfs): 3.6 GB partition, **1.9 GB used (54 %)**
- `/dev/mmcblk0p1` (`/boot/uboot`, FAT): 96 MB, 83 MB used

Current *content* is ~1.9 GB, so the data fits a 4 GB card. ⚠️ The concern for #6 is the
**target image's flashed rootfs size**, not ours — a console/IoT image should fit 4 GB; a
graphical/full image may not. Confirm the chosen image's flashed size before flashing.

## Boot → clock launch chain (what must be reproduced on the new image)

1. **u-boot** reads `/boot/uboot/uEnv.txt`; `optargs=... capemgr.enable_partno=BB-SPI0-01`
   loads the SPI0 overlay.
2. **`/etc/rc.local`** re-loads `BB-SPI0-01` into capemgr slots, then runs
   `/etc/init.d/clock start`.
3. **`/etc/init.d/clock`** (LSB sysvinit script, generated from `clock.metainit`)
   daemonises `/usr/local/bin/clock -t` via `start-stop-daemon`.
   `Required-Start: tsl2561-daemon $syslog $time` — **clock must start after the light
   sensor daemon.**
4. **`tsl2561-daemon`** (`/usr/local/bin/tsl2561-daemon`, its own `/etc/init.d/`
   service) — the ambient-light sensor daemon that feeds brightness.

**Binary locations:** `/usr/local/bin/clock` (arg `-t`), `/usr/local/bin/tsl2561-daemon`.
Old binary variants also present: `clock.old`, `clock~`, `clock.pre-p1-unblank-fix`.

## Two services to recreate as systemd units

| sysvinit today | systemd target | notes |
|---|---|---|
| `/etc/init.d/tsl2561-daemon` | `tsl2561-daemon.service` | light sensor; start first |
| `/etc/init.d/clock` → `clock -t` | `clock.service` | `After=tsl2561-daemon.service`; SPI must be up |

Generated-from-metainit artifact: `tsl2561-daemon/init/…metainit`. On systemd the
`Required-Start` ordering becomes `After=`/`Wants=`.

## Overlays actually in use

- **`BB-SPI0-01`** — the only overlay loaded (SPI0 for the LED chips). **Must be
  regenerated** for the new kernel's overlay ABI and loaded via `uEnv.txt`
  (`/boot/firmware`) instead of capemgr. Repo has `BB-SPI0-01-00A0.dts/.dtbo`.
- **`BB-BLANK`** — present in repo (`BB-BLANK-00A0.dts`) but **NOT loaded** (not in
  capemgr slots). Not part of the live P1 fix. Do not carry it over unless a new need
  appears.

## P1 / P2 re-verification hazards on the new image

- **P1 (ADR 0001)** — the fix is **in `clock.c`**, not an overlay: `unblank_display()`
  drives **gpio20** high after the first valid frame, and the old early-unblank
  `displayon` service was disabled (`S05→K05`). Migration hazards:
  - **gpio20 numbering changes** across kernels (3.8 global bank numbering vs newer
    gpiochip-relative), and **sysfs gpio is deprecated/removed** on recent kernels
    (may need libgpiod / character device). `unblank_display()` may need rework.
  - On systemd there is no `rc2.d`; simply **do not** create any unit that unblanks the
    display early — the hardware pull-down + `clock`'s self-unblank keep it fail-safe.
  - Re-run the **cold-boot** acceptance test (display stays blank through boot, no
    garbage flash).
- **P2 (ADR 0002)** — hardware fix (twisted-pair grounds); should be unaffected, but
  re-run the **`p2stress`** sweep on the new image to confirm no regression from timing
  changes.

## Rollback

Keep the **current SD card untouched**; flash the new image to a *separate* card so the
old card is a drop-in rollback. Backup (user-run `tar | bzip2`) must be confirmed before
any flashing.
