# ADR 0002 — P2 root cause: ribbon crosstalk from CLK/DIN into the LATCH line

- **Status:** Accepted (root cause established by on-device stress testing + photos +
  independent corroboration; hardware fix not yet applied)
- **Date:** 2026-07-05
- **Fault:** P2 / runtime random-bright glitch (see `CONTEXT.md`, `README.org` → *Findings*)
- **Supersedes the open question in** `docs/diagnostic-plan.org` Phase 2/3

## Context — what we found

P2 is intermittent, self-healing, position-varying bright-channel corruption during
normal running. Using the `p2stress` harness (drives grayscale directly, bypasses the
ambient/brightness path) on the live device, at DIM levels:

- **Arm A (static hold, zero bus activity), cold, 20 min: no glitches.** So it is NOT
  spontaneous bit-rot while the latch holds.
- **Arm B (hammering the latch while sending): glitches appear.** So P2 is **triggered
  by sending/latching**, not by holding.
- **SPI-clock sweep** (same 10 Hz latch rate, hole pattern, ~flickers/30 s):
  - 100 kHz → ~45
  - **1 MHz → ~20 (minimum)**
  - 4 MHz → near-constant
  A **U-curve with the minimum at 1 MHz** (the speed the clock already runs at). Being
  SPI-clock-dependent proves the corruption is in the **serial data path**, not random
  VCC bit-flips (those would be clock-speed-independent).
- **Spatial signature:** glitches hit **bursts of consecutive channels** — most often
  the bottom bars C,D,E of a digit, which map to 3 *consecutive* channel numbers
  (e.g. digit 2 = channels 0,1,2). A corrupted *run* of adjacent channels = a burst
  error in the shift stream.
- **Chip A bias**, confirmed in photos (`ti-forum/IMG_596*.jpeg`): the right three
  digits (Chip A) show bright garbage while the leftmost (Chip B) stays dim; the
  garbage pattern differs frame to frame.
- **Direction:** channels jump **brighter** — data bits flipped on.

### The wiring

Signals travel BBB↔board on a **6-wire ribbon**. Board header order (top→bottom):

```
LAT  /OE  CLK  DIN  GND  V+
```

The BBB's **SPI chip-select is used as the LATCH (XLAT)**, so SCLK, DIN, and the latch
all share this one ribbon. In this pinout **CLK sits one wire from LAT, directly against
/OE and DIN, and the only ground is stranded at the far end** — there is no ground shield
between the switching clock and the latch line.

### Independent corroboration

Adafruit forum thread p=646976 (saved under `ti-forum/`), post 9: a builder with 9
TLC5947s and the *same* "intermittent flashing and fluttering in random parts" tried
**1000 µF caps per module**, a resistor swap, and heat sinks — **none fixed it**. The
culprit he isolated: running the **data/clock cable physically close to the latch/OE
cable** — *"the crosstalk between these two cables is very disruptive to correct
operation."*

## Decision — root cause

**P2 is signal-integrity crosstalk on the ribbon: the switching CLK (and DIN) edges
couple into the CS/LATCH line, causing a mistimed/spurious latch that captures a
partially-shifted frame.** A latch that fires mid-shift latches whatever is in the
grayscale shift register at that instant → a run of adjacent channels gets the wrong
(brighter) values → bright garbage that self-heals on the next clean frame.

This single mechanism explains every observation: triggered only by sending (Arm B),
worse with faster/edgier clock (4 MHz), bursts of consecutive channels (partial shift
captured), brighter (bits flipped on), and Chip A bias (cable routing / chain position).

It is **not** a `clock.c` logic bug (self-heals; bounded data) and **not** primarily a
VCC/decoupling problem (independent evidence: 1000 µF caps did not help).

## Consequences / fix (hardware — highest value first)

1. **Interleave grounds in the ribbon** so every switching signal (LAT, /OE, CLK, DIN)
   is flanked by a ground wire tied to ground **at both ends** (BBB GND and the board
   GND/plane). ~10–11 wires. The header pin order need not change; grounds are inserted
   between the existing signals. The single most important one is a **ground between
   CLK and LAT/OE**.
2. **Series ~33–100 Ω on CLK** at the BBB end to damp the edges that do the coupling.
3. **Shorten the run**; keep CLK/DIN physically away from LAT/OE.
4. **Cheap test-first:** per post 9, just split CLK/DIN into a separate bundle routed
   away from LAT/OE and confirm the flicker drops before building the full harness.

**Deprioritised:** the plan's "Phase 1 ~100 µF on VCC" as a P2 fix — independent
evidence (1000 µF, above) says caps do not fix this. (VCC/ground hygiene is still
generally good, but it is not the P2 root cause.)

**Do NOT** cut the SPI clock "for margin" — 1 MHz is already the U-curve minimum;
100 kHz was worse.

## Verification (after the ribbon change)

Re-run `p2stress -a b -p hole -g 100 -s 4000000 -i 100000` (the harshest case, which
was near-constant before) and confirm the flicker rate collapses. Then re-run at 1 MHz
and confirm it is effectively gone under stress. Tick the Phase 2/3 boxes in
`docs/diagnostic-plan.org`.

## Related

- `p2stress.c` — the harness (Arm A/B, hole/walk patterns, `-s`/`-i` sweeps).
- Photos: `ti-forum/IMG_596*.jpeg`. Forum capture: `ti-forum/646976*.html`.
- P1 power-up fix is separate: see `docs/adr/0001-default-blank-power-up.md`.
