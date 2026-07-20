# Blog Post Scaffold: "Chasing a Flicker Down to the Wire"

> Scaffold only — beats to hit + the concrete facts to draw on. Write the prose
> in your own voice. Source: `docs/adr/0002-p2-ribbon-crosstalk.md`, `CONTEXT.md`.

## Title options
- *Chasing a Flicker Down to the Wire* — the crosstalk pun
- *The Clock That Glitched Only When It Talked*
- *A U-Curve, a Stress Harness, and One Missing Ground*
- *When the Bug Is in the Cable, Not the Code*

## Framing / hook (1 short para)
- Open on the symptom a person actually sees: a wall clock that, every so often,
  flashes a few segments much too bright for about half a second, then fixes
  itself. Rare, random, never the same spot twice.
- Set up the twist: this looked like a software bug for a long time and turned out
  to live in a ribbon cable. Promise the reasoning chain, not just the answer.

---

## 1. Brief history of the project
Keep it tight — enough for a stranger to picture the rig.
- What it is: a BeagleBone Black driving a four-digit seven-segment LED clock.
- Drivers: two daisy-chained **TLC5947** 24-channel constant-current PWM LED
  drivers = 48 channels; Chip A = right three digits, Chip B = leftmost digit + colon.
- One detail that matters later: the BBB's **SPI chip-select is repurposed as the
  latch (XLAT)** — so clock, data, and latch all ride the *same* ribbon.
- Optional color: a long-lived hobby build, code compiled *on the device*
  (gcc 4.6.3), the kind of project where the schematic lives half in your head.

## 2. The problem (define it precisely)
- Name the two faults so readers know which one this is: power-up garbage (P1) vs.
  the runtime glitch (**P2**) — this post is P2.
- Precise definition of P2: intermittent, self-healing, position-varying
  **bright-channel** corruption during normal running. Channels only ever jump
  *brighter*, never dimmer.
- Why "self-heal" matters as a clue: the display refreshes ~twice a second, so a
  bad frame is overwritten within ~500 ms — that's why it flashes rather than sticks.
- The frustrating part to convey: it's rare and random, so you can't just stare at it.

## 3. First deductions before touching hardware
Show reasoning ruling things out.
- **Constant-current sink logic:** brightness is set by PWM duty, not supply
  voltage — so a sagging supply could only *dim* an LED, never brighten one. A
  glitch that goes *brighter* is data corruption, not a power droop. (Good
  counterintuitive beat.)
- **Not a `clock.c` logic bug:** a framing/off-by-one bug would corrupt the *same*
  channel *every* frame. P2 is random and self-heals → physical-layer, not logic.
- Going in, the hypothesis space narrows to: something corrupts the serial
  data/latch path intermittently.

## 4. How we diagnosed it — the stress harness
The heart of the post. The move: stop waiting for a rare bug, *provoke* it.
- Built **`p2stress`**, a small on-device harness that drives grayscale directly
  and bypasses the normal ambient-light/brightness path — testing the bus, nothing else.
- Two "arms" as a controlled experiment:
  - **Arm A — static hold:** load a pattern, zero bus activity. Cold, 20 min →
    **no glitches.** Conclusion: *not* bit-rot in the latch while it just sits there.
  - **Arm B — hammer:** keep sending and latching. Glitches appear. Conclusion: P2
    is **triggered by sending/latching**, not by holding. (The pivotal fork.)
- The clincher — **sweep the SPI clock** (same 10 Hz latch rate, same pattern,
  count flickers per 30 s):

  | SPI clock | flickers / 30 s |
  |---|---|
  | 100 kHz | ~45 |
  | **1 MHz** | **~20 (minimum)** |
  | 4 MHz | near-constant |

  A **U-curve with the minimum right where the clock already runs (1 MHz).** The
  inference: if corruption depends on *clock speed*, it's in the **serial data
  path**; random VCC bit-flips would be clock-speed-independent. Faster, edgier
  clock = more corruption = signal integrity.
- **The spatial tell:** glitches hit **bursts of consecutive channel numbers**
  (e.g. the bottom bars C/D/E of a digit map to 3 adjacent channels). A corrupted
  *run* of adjacent channels = a burst error in the shift stream = a latch that
  fired *mid-shift* and captured a half-shifted frame.
- **Chip A bias**, visible in photos: right three digits show garbage while the
  leftmost stays clean — points at cable routing / chain position, not a chip defect.

## 5. The "aha" — reading the wiring
- Reveal the ribbon pinout and let the reader see it:

  ```
  LAT  /OE  CLK  DIN  GND  V+
  ```

- The problem in one sentence: **CLK sits one wire from LAT, no ground between
  them, and the only ground is stranded at the far end.** The switching clock's
  edges couple into the latch line → a spurious/mistimed latch → captures a
  partially-shifted frame → a run of adjacent channels latches wrong, brighter values.
- Tie it back: this single mechanism explains *every* observation — triggered only
  by sending (Arm B), worse at 4 MHz, bursts of consecutive channels, always
  brighter (bits flipped on), Chip A bias.

## 6. Corroboration (optional but strengthens it)
- Not the only one: an Adafruit forum builder with 9 TLC5947s and the same
  "flashing/fluttering" tried 1000 µF caps, resistor swaps, heat sinks — *none
  worked*; the fix was separating the data/clock cable from the latch cable.
  Independent confirmation that caps are a red herring and the **latch line** is the
  weak point.
- A second thread (BBB + TLC5947 chain, 1 MHz+, same flicker) had experts naming
  **ringing on the latch lines** and clock/latch skew. Reassures the reader the
  diagnosis isn't a lone guess.

## 7. The fix
- Chosen: **rewire the ribbon with twisted-pair ground returns** (Cat5e). Each of
  the four switching lines gets its *own* twisted ground return, grounded at **both
  ends**; V+ run separately so LED-rail current stays out of the signal bundle.

  | Pair | Signal | Return |
  |---|---|---|
  | 1 | CLK | GND |
  | 2 | LAT (CS/XLAT) | GND |
  | 3 | DIN | GND |
  | 4 | /OE | GND |

- Why twisted pairs over just interleaving flat grounds: same labor to re-pull the
  cable, but you also get inductive-coupling cancellation from the twist — each
  aggressor's return is *wrapped around it*, not just laid beside it.
- What you *didn't* need: no series resistors, no Schottky termination, no bigger
  caps, and **not** slowing the SPI clock (1 MHz was already the U-curve minimum —
  going slower made it worse). Grounds alone did it. A "resist over-engineering" note.

## 8. Verification / result
- Re-ran the harshest harness case on the full two-chip display, someone watching
  and counting:

  | SPI clock | Pre-fix / 30 s | Post-fix |
  |---|---|---|
  | 100 kHz | ~45 | **0** |
  | 1 MHz | ~20 | **0** |
  | 4 MHz | near-constant | **0** |

- Punchline: the near-constant 4 MHz case collapsed to **zero**, across both chips.
- Honesty caveat: detection was *visual* (no scope readback), so a human watched
  and counted.

## 9. Takeaways (pick 2–3, don't list all)
- "Bug goes *brighter* not dimmer" was the first fork that ruled out power and
  pointed at data — read your symptom's direction.
- The best move on a rare intermittent bug is to **build the thing that provokes it
  on demand** (Arm A/B + a parameter sweep turns "spooky and random" into a graph).
- Signal integrity is invisible in the code — sometimes the fix is a ground wire,
  not a commit.
- Reusing SPI chip-select as the latch put the most timing-critical line right next
  to the noisiest one. Cheap to do, expensive to debug.

---

## Assets to consider
- Photos: `ti-forum/IMG_596*.jpeg` (the Chip A garbage).
- Redraw the ribbon pinout + the U-curve as simple graphics — the U-curve chart
  alone can carry section 4.

## Audience note
- General dev blog → lean on the "build a harness to trap a rare bug" arc (§4).
- Electronics audience → give §5–7 more room.
