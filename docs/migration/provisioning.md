# New-image provisioning — hostname, user, SSH keys, wifi, packages

How to bring the freshly-flashed Debian 13.5 card up as `clock` with your access and
wifi, and install the (minimal) package set the clock needs. Companion to
[`target-image-plan.md`](target-image-plan.md). Confirmed against the mounted image
2026-07-08.

Scope decision (2026-07-08): **clock only.** No apache / bind9 / ddclient / exim /
hostapd-AP are carried over — the old box accumulated those for unrelated projects.

## Mechanisms on this image (confirmed)

- **First-boot config:** `bbbio-set-sysconf` (enabled via `basic.target.requires`) reads
  `/boot/firmware/sysconf.txt` once, applies it, then **wipes** it. Keys: `hostname`,
  `timezone`, `keymap`, `user_name` (user 1000), `user_password`, `root_password`,
  `user_authorized_key`, `root_authorized_key` — **one key per field.**
- **Default user:** `debian` (uid 1000); `user_name=jason` renames it.
- **sshd:** default `PermitRootLogin prohibit-password` → **root login by key works** out
  of the box (matches the `root@clock.local` workflow).
- **mDNS:** `avahi-daemon` is present → `clock.local` resolves once `hostname=clock`.
- **Network:** `systemd-networkd` + **`iwd`** (no wpa_supplicant/NetworkManager). Wifi is
  configured by dropping `/var/lib/iwd/<ssid>.psk` files. `wlan0.network` already does
  DHCP.

## The card-provisioning bundle

Staged in the session scratchpad at `card-bundle/` (kept out of the repo — it contains
wifi passphrases). Apply it to the card **after flashing, before first boot**:

| Bundle file | Goes to (on card) | Notes |
|---|---|---|
| `sysconf.txt` | `/boot/firmware/sysconf.txt` (p1, FAT) | hostname/tz/keymap, `user_name=jason`, bootstrap user key |
| `root-ssh/authorized_keys` (9 keys) | `/root/.ssh/authorized_keys` (p3) | mode 600, owner root. `/root` is a stable path → safe to pre-place |
| `iwd/*.psk` (8 networks) | `/var/lib/iwd/` (p3) | mode 600, owner root. Stable path → wifi works first boot |
| `user-ssh/authorized_keys` (17 keys) | **post-boot** append (see below) | jason's home path isn't final until the rename runs |

**Why the split:** `user_name=jason` renames `debian`→`jason` during first boot, so the
final `/home/jason` doesn't exist when we prep the card. We therefore let `sysconf` seed a
single **bootstrap** key into whatever home it ends up with (guarantees first-boot
`jason@clock.local` from this workstation), and append the full 17-key ring afterward.
Root's home (`/root`) is stable, so root's full ring is pre-placed and `root_authorized_key`
is deliberately left unset in `sysconf.txt` (setting it could clobber the pre-placed file).

### Apply to card (rootfs = p3, boot = p1)
```sh
# with the card's partitions mounted at $BOOT (p1) and $ROOT (p3):
cp card-bundle/sysconf.txt        "$BOOT/sysconf.txt"
install -d -m700 -o0 -g0          "$ROOT/root/.ssh"
install -m600 -o0 -g0 card-bundle/root-ssh/authorized_keys "$ROOT/root/.ssh/authorized_keys"
install -d -m700 -o0 -g0          "$ROOT/var/lib/iwd"
cp card-bundle/iwd/*.psk          "$ROOT/var/lib/iwd/"; chmod 600 "$ROOT"/var/lib/iwd/*.psk
```

### Post-boot (once `jason@clock.local` works)
```sh
# append jason's full keyring
ssh jason@clock.local 'install -d -m700 ~/.ssh && cat >> ~/.ssh/authorized_keys' \
    < card-bundle/user-ssh/authorized_keys
```

## Wifi

One network staged: **`buffalo-n`** — the network the current clock is actually associated
to. (The old box's `wpa_supplicant.conf` held 8 saved networks; the rest were stale and
were dropped. The full set is still recoverable from the old card if needed.)

**Dongle works out of the box — no firmware install needed.** The adapter is an
**ath9k_htc** (USB `13d3:3327`, Atheros AR9271). The image already ships both the
`ath9k_htc` kernel module *and* the matching firmware `htc_9271.fw` (in its curated
`/lib/firmware` set, even without the full `linux-firmware` package). AR9271 is
2.4 GHz-only, so the absent `regulatory.db` (world regdom, channels 1–11) is a non-issue.
→ **The BBB can come up directly on wifi on first boot with no ethernet** — just plug the
dongle in before powering on and stage the `.psk` files below.

## Minimal package install (on the booted new box)

Already in the base image — **nothing to do**: `build-essential` (gcc-14 + make),
`i2c-tools`, `git`, `curl`, `rsync`, `avahi-daemon`/`avahi-utils`, `python3`,
`device-tree-compiler`, `iwd`, `systemd-timesyncd`.

Actually needed: **essentially nothing.** Wifi firmware (`htc_9271.fw`) is already in the
image, so no `firmware-atheros`. The only conditional install:
```sh
# Only if tsl2561-daemon fails to build for a missing <linux/i2c-dev.h> on gcc-14:
sudo apt install -y libi2c-dev
```
`minimal` per your call — personal CLI tools (vim, screen, gdb, strace, …) are **not**
installed here; add them yourself later. The full old-box manual package list (266 pkgs,
mostly Wheezy base + emacs-build dev-libs) is archived at
[`clock-packages-manual.txt`](clock-packages-manual.txt) for reference — do not replay it
verbatim (names changed across 6 Debian releases).

## Services

Install the two systemd units from [`../../deploy/systemd/`](../../deploy/systemd/):
`tsl2561-daemon.service` (starts first) and `clock.service` (`Requires`/`After` it). See
[`target-image-plan.md`](target-image-plan.md) for the binary rebuild + the spidev/i2c
path changes.
