# On-device build & deploy runbook (BBB clock)

How to compile `clock` (and helpers like `p2stress`) **on the BeagleBone itself**,
deploy a new binary, and roll back. The BBB is the build host — there is **no
cross-compile path**.

## Why build on-device

- The BBB runs **Debian 7 (wheezy)**, **gcc 4.6.3**, **EGLIBC 2.13**, kernel 3.8.13.
- That toolchain predates `-std=gnu11`/`-std=c17`. The repo `Makefile` uses
  `-std=c17 -Werror`, which is for a *modern* host only and **will not compile on
  the device**. Do not use the Makefile on the BBB.
- The dev laptop is x86 with a far newer glibc; a cross-built binary would link
  against the wrong glibc. So we compile natively on the target (same approach the
  P2 `p2stress` harness used — see its header comment).

## Access

- SSH: `root@192.168.1.28` (mDNS name `clock.local` is flaky; the raw IP is
  reliable). See the `bbb-access-and-backup` project memory.
- The old sshd needs `ssh-rsa` re-enabled. Either use the `Host clock.local`
  block in `~/.ssh/config` (which sets `PubkeyAcceptedAlgorithms +ssh-rsa`), or
  when using the raw IP pass it explicitly:
  ```sh
  ssh -o PubkeyAcceptedAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa \
      -i ~/.ssh/id_rsa root@192.168.1.28
  ```

## Canonical source

The **repo on the dev machine** (this checkout) is the source of truth. The
on-device `/home/jason/clock/` directory is a **stale 2015 checkout** (git
`55418b3`, no Makefile) — do **not** build from it. Copy the current `clock.c`
from the repo to the device for each build.

## Build command (the thing that actually works on the BBB)

Native, gcc 4.6.3, C99 dialect, link libm for `round()`/`math.h`:

```sh
gcc -std=gnu99 -O2 -Wall -o clock clock.c -lm
```

Notes:
- **`-std=gnu99`** — gcc 4.6.3 has no `gnu11`/`c17`.
- **`-lm`** — `clock.c` uses `round()`/`min`/`max` from `math.h`.
- **No `-Werror`** — old EGLIBC may warn (e.g. on `_DEFAULT_SOURCE` feature-macro
  gaps for `usleep`); those are harmless. `p2stress` built with `-std=gnu99 -O2
  -Wall` (no `-Werror`); mirror that.
- The running service binary lives at **`/usr/local/bin/clock`** (started by
  `rc.local` → `/etc/init.d/clock start`). `/home/jason/clock/clock` and
  `start-clock.sh`'s `/root/clock/clock` are stale references.

## Deploy a new clock binary (safe sequence)

```sh
# 0. on dev machine: copy current source to device
scp -o PubkeyAcceptedAlgorithms=+ssh-rsa -i ~/.ssh/id_rsa \
    clock.c root@192.168.1.28:/root/clock-build.c

# on device:
# 1. back up the running binary (once, before first deploy)
cp -av /usr/local/bin/clock /usr/local/bin/clock.pre-<change>   # e.g. clock.pre-p1-unblank-fix

# 2. build natively
cd /root && gcc -std=gnu99 -O2 -Wall -o clock-build clock-build.c -lm
file clock-build            # expect: ELF 32-bit LSB executable, ARM

# 3. stop the service, install, restart
/etc/init.d/clock stop
cp -av clock-build /usr/local/bin/clock
/etc/init.d/clock start
```

Rollback of the binary:
```sh
/etc/init.d/clock stop
cp -av /usr/local/bin/clock.pre-<change> /usr/local/bin/clock
/etc/init.d/clock start
```

## Verifying the display / gpio state

```sh
grep gpio-20 /sys/kernel/debug/gpio      # "out hi" = unblanked/enabled; "out lo"/absent = blanked
/usr/local/bin/displayoff.sh             # gpio20 -> 0 = blank
/usr/local/bin/displayon.sh              # gpio20 -> 1 = unblank
```

---

## Deployment record — P1 "unblank after first frame" (2026-07-07)

**What changed & why.** P1's residual symptom was: display blank at power-on
(hardware pull-down works), then **all segments flash full-bright later in boot**.
Root cause (confirmed on device) was a **boot-order race**: the `displayon` init
service unblanked gpio20 very early (`S05`), but `clock` — which writes the first
valid frame — only starts at the *end* of boot from `rc.local` (which is also
where the SPI overlay loads). So the power-on grayscale-latch garbage was made
visible for the whole boot. Fix: **`clock` unblanks itself after its first frame**,
and the early `displayon` unblank is **disabled**.

### Changes applied to the device (192.168.1.28)

1. **`/usr/local/bin/clock`** replaced with a build of the new `clock.c`
   (adds `unblank_display()`, called once after the first `write_led_buffer()` in
   `clockfn()`).
   - New binary md5 `70232d2109725b231fa31537aea59877`.
   - **Backup of the previous binary:** `/usr/local/bin/clock.pre-p1-unblank-fix`
     (md5 `bbd45909809a3541603efb717b1376d2`).
   - Build artifacts left in `/root/`: `clock-p1.c`, `clock-p1`, `clock-p1-warmtest.log`.
2. **`displayon` boot service disabled** so nothing unblanks early. The start-links
   were converted to stop-links:
   - **Before:** `/etc/rc{2,3,4,5}.d/S05displayon`, `/etc/rc1.d/K01displayon`.
   - **After:**  `/etc/rc{2,3,4,5}.d/K05displayon`, `/etc/rc1.d/K01displayon`.
   - `displayon`'s `stop` action is a no-op for gpio20 (`displayoff.sh` is dead code
     after `return` in `do_stop`), so the stop-links are harmless.
   - NOTE: `update-rc.d displayon disable` **errors** ("insserv: Service clock has
     to be enabled…") because `/etc/init.d/displayon`'s header declares
     `Required-Start: clock` while `clock` is started from `rc.local`, not insserv.
     The disable still renamed the symlinks before insserv choked. Manage these
     links **by hand** (below), not via `update-rc.d`.
   - `displayon.sh` / `displayoff.sh` remain installed as manual tools; only the
     boot-time auto-unblank is removed.

### Rollback (restore pre-fix behaviour)

```sh
# 1. restore the old clock binary
/etc/init.d/clock stop
cp -av /usr/local/bin/clock.pre-p1-unblank-fix /usr/local/bin/clock
/etc/init.d/clock start

# 2. re-enable the early displayon unblank (S05) exactly as it was
for rl in 2 3 4 5; do mv /etc/rc$rl.d/K05displayon /etc/rc$rl.d/S05displayon; done
# rc1.d/K01displayon was never changed.
```

### Verification status

- **Warm-tested OK (2026-07-07):** with the display manually blanked
  (`displayoff.sh` → gpio20 `out lo`), starting the new binary drove gpio20
  `out hi` within ~1 s (i.e. only after the first frame). Daemon healthy.
- **PENDING — cold-boot test (user's hands):** full power-cycle with display
  connected; expect **no lit LEDs at any point** through boot until `clock` starts,
  then normal operation. This is the real P1 acceptance test.
</content>
