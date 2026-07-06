# ADR 0001 — Make "blanked" the fail-safe power-up default (fix P1)

- **Status:** Amended — implemented via a software boot-order fix (see *Update
  2026-07-07*); cold-boot acceptance test still pending on device.
- **Date:** 2026-07-05 (amended 2026-07-07)
- **Fault:** P1 / power-up garbage (see `CONTEXT.md`, `README.org` → *Findings*)
- **Relates to:** `docs/diagnostic-plan.org` Phase 4,
  `docs/agents/device-build-deploy.md` (build/deploy/rollback record)

## Update (2026-07-07) — actual root cause and implemented fix

The hardware default turned out **not** to be the live problem: with the current
board, the display is **already blank at power-on** (confirmed by the user). The
residual symptom was different — the display comes up blank, then **flashes all
segments full-bright *later* in boot**.

Diagnosed on device as a **boot-order race**, not a hardware-default issue:

- Blanking is driven entirely by the `displayon`/`displayoff` init service via
  gpio20; **`clock.c` never touched gpio20**, so unblank and data-write were
  fully decoupled.
- `displayon` unblanked **early** (`/etc/rc2.d/S05displayon`, i.e. `S05`), but
  `clock` — which writes the first valid frame — is not insserv-managed and only
  starts at the **end of boot from `rc.local`** (which is also where the SPI
  overlay is loaded, so `clock` *cannot* write to the chips any earlier). The
  power-on grayscale-latch garbage was therefore displayed for the whole boot,
  from `S05` until `rc.local`.

**Implemented fix (supersedes the Decision below as the actual remedy):**

1. **`clock` unblanks itself after its first frame.** New `unblank_display()` in
   `clock.c` drives gpio20 high, called **once**, immediately after the first
   `write_led_buffer()` in `clockfn()`. Unblank is now *causally after* valid
   data, so no timing race remains. Failure fails safe (display stays blank).
2. **The early `displayon` boot-unblank is disabled** (`S05` → `K05` stop-links
   in runlevels 2–5). Nothing unblanks early; the hardware pull-down keeps the
   display dark from power-on until `clock` writes + unblanks.

This revises the ADR's original "**`clock.c` never touches gpio20**" stance — the
decoupling of unblank from data-write *was* the defect. The hardware pull-down
(Decision item 1 below) remains valid defense-in-depth for the pre-`clock` window
but is no longer the load-bearing fix. Exact device changes, build command, and
rollback are in `docs/agents/device-build-deploy.md`. Warm-tested OK; cold-boot
power-cycle acceptance test still pending (user's hands).

---

_Original ADR (2026-07-05) follows._

## Context

At cold boot the display shows random bright LEDs until the software writes valid
data (**P1 / power-up garbage**). The mechanism (from `README.org`):

- Each TLC5947's **grayscale latch holds undefined values** at power-on.
- Whether those values are *shown* depends only on the chip's **BLANK** (/OE) input.
- On the Adafruit board, `R3` (10 kΩ) pulls **BLANK low = outputs enabled** by default.
- BLANK is actually driven by a **BC548** transistor stage off the BBB's **gpio20**
  (header **P9_41**):

  ```
  gpio20 ──10kΩ──► BC548 base      collector ──► /OE (BLANK), and ──1kΩ──► +5V
                        emitter ──► GND
  ```

  - gpio20 **high** → BC548 **on** → /OE **low**  → **enabled**
  - gpio20 **low**  → BC548 **off** → /OE **high** → **blanked**

- During boot, gpio20 is a **hi-Z input** and the BC548 base floats. P9_41 is a
  BBB "double pin" (also wired to GPIO3_20 / gpio116) and likely boots with an
  internal pull-up. A floating/high base turns the BC548 on → /OE leaks low →
  the **undefined latch contents are displayed** until `displayon.sh` / `clock` run.

So P1 exists because **"enabled" is the default state** — blanking depends on
software winning a race against the boot window.

### Why software/boot-order tweaks are not enough

The current SPI overlay (`BB-SPI0-01`) is loaded from **`rc.local`**, which runs
**late in userspace**. Any pinmux applied that way (including a pull-down on
gpio20) takes effect *after* the early-boot window where the garbage appears —
too late to prevent P1. To matter at all, a pinmux fix must be applied **early**
(U-Boot, the base DTB, or an auto-loaded cape via `uEnv.txt`), not from `rc.local`.

## Decision

Make **blanked** the fail-safe hardware default, so that *no active drive =
blanked* and blanking never depends on software timing:

1. **(Primary, load-bearing) Hardware pull-down on the BC548 base.**
   Add a resistor (~10 kΩ) from the **BC548 base to GND** (base-to-emitter).
   - With no active drive (boot hi-Z, or gpio20 low), the base is held low →
     BC548 **off** → /OE high → **blanked**, from the instant power is applied.
   - When gpio20 drives **high** (3.3 V through the existing 10 kΩ), the base still
     clamps at ~0.7 V and receives ~0.19 mA (0.26 mA source − 0.07 mA into the
     10 kΩ pull-down); with BC548 hFE ≈ 200+ that saturates the ~5 mA /OE pull-up
     load easily. So normal "enable" still works.
   - This fix is **local to the transistor** and therefore immune to gpio boot
     state, the P9_41 double-pin, pinmux, and boot ordering. This alone resolves P1.

2. **(Secondary, defense-in-depth) Early device-tree pinmux on gpio20.**
   Mux **P9_41 / gpio0_20** as GPIO with an **internal pull-down**, applied early
   (not from `rc.local`), and mux the double-pin sibling **gpio3_20 / gpio116** to a
   passive input so it cannot drive P9_41. Draft overlay: `BB-BLANK-00A0.dts`
   (offsets flagged for verification — see that file). This hardens the default
   but is **not sufficient on its own** given the late-load constraint above, and
   is optional once the base pull-down is in place.

`clock.c` is unchanged: it never touches gpio20 today, and it should not need to.
`displayon.sh` continues to enable the display after `clock` has written real data.

## Consequences

- **Positive:** Cold boot shows **no LEDs** until `clock` writes valid data. P1 is
  eliminated at the hardware layer, independent of software/boot races.
- **Positive:** Same change makes the display safely dark during any future crash,
  service restart, or reflash where gpio20 is not being actively driven high.
- **Cost:** Requires a soldering change (one resistor) on the blanking board — a
  human-hands task. The pinmux overlay, if adopted, must be wired into the early
  boot path.
- **Neutral:** Slightly reduced base drive margin (quantified above; still saturates).
- **Interaction with P2:** None. P1 is the power-up latch-visible-through-boot
  problem; P2 is runtime grayscale corruption (separate root cause, Phases 0–3).
  This ADR does not touch the refresh loop.

## Verification (to run after the hardware change)

1. Fully power-cycle the BBB (cold boot), **display connected**.
2. Observe from power-on through the boot until `clock` starts: expect **no lit
   LEDs at any point** (previously: random bright segments during boot).
3. Confirm `clock` still lights the display normally once running
   (`gpio-20 ... out hi` in `/sys/kernel/debug/gpio`, digits correct).
4. Sanity: `displayoff.sh` (gpio20 → 0) blanks; `displayon.sh` (gpio20 → 1) enables.
5. Tick the Phase 4 boxes in `docs/diagnostic-plan.org` and set this ADR to
   **Accepted**.

## Open items

- Confirm the exact **P9_41 pad control-register offsets** used in
  `BB-BLANK-00A0.dts` against the AM335x TRM / BBB SRM before compiling/flashing.
- Decide whether the pinmux overlay is worth wiring into the early boot path, or
  whether the base pull-down alone is sufficient (recommended: base pull-down
  alone; treat the overlay as optional).
