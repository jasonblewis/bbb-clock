# Handoff — verify the P2 fix after the ribbon-cable upgrade

## 📍 STATUS (updated 2026-07-06) — Chip B verified, Chip A pending
Verification is **half done**. The user rewired the **first TLC5947 only** — Chip B
(leftmost hours-tens digit + the 2 colons, logical channels 24–47) — with the CLK↔LAT
ground returns as **twisted pairs back to the BBB**. The second chip (Chip A, the other
3 digits) is **not yet wired**.

Ran the sweep on Chip B in isolation (`p2stress -a b -p hole -g 100 -i 100000 -w 1000`,
DIM), user watching:

| SPI clock | Pre-fix / 30 s | Post-fix (Chip B, twisted grounds) |
|---|---|---|
| 100 kHz | ~45 | **0 — rock solid** |
| 1 MHz | ~20 | **0** |
| 4 MHz | near-constant | **0** |

→ Twisted-pair grounds alone fixed this ribbon run; **series-R / Schottky fallback NOT
needed**. Note the harness has no chip-select (`-a` is arm, not chip) and shifts all 48
channels regardless — with one chip connected the `hole` uniform-dim field still works as
a glitch-watch, though the *displayed* digits may not be the "expected" ones.

**NOT signed off yet.** ADR 0002 was deliberately left at its prior status; Phase 2/3
boxes NOT ticked; PR #1 not updated. That waits until Chip A is wired the same way and
the **full 2-chip assembly** passes the identical sweep. **Clock left STOPPED** during
wiring (`/etc/init.d/clock start` to restore).

**Next:** user wires Chip A → re-run the sweep on both chips → if clean, do the sign-off
steps in "Verification procedure" step 5 below.

---

## ⚠️ WHERE TO WORK — read first
All the harness code and ADRs live on branch **`p2-stress-harness`**, checked out in the
worktree at:

```
/home/jason/nextcloud/projects/bbb-clock/.claude/worktrees/p2-stress-harness
```

**`cd` into that worktree** (or `EnterWorktree` with `path:` pointing at it) before you
start — the **main checkout is on `master` and does NOT contain** `p2stress.c`,
`docs/adr/0002-*`, `docs/adr/0001-*`, or the updated README/plan. The branch is also on
GitHub as **PR #1** (https://github.com/jasonblewis/bbb-clock/pull/1), so if the worktree
was removed you can recreate one from `origin/p2-stress-harness`.

---

You're picking up a **hardware-debugging** effort on a BeagleBone Black (BBB) driven
TLC5947 7-segment LED clock. The **P2 runtime glitch has already been diagnosed** in the
previous session; the user is now **rewiring the ribbon cable** to apply the fix. **Your
job is to verify the fix worked** using the existing on-device stress harness, then close
out the docs.

## Don't re-derive — it's all written down
- **Root cause + fix + evidence:** `docs/adr/0002-p2-ribbon-crosstalk.md` (this is the
  key doc). TL;DR: P2 is **ribbon crosstalk** — the switching CLK/DIN edges couple into
  the CS/LATCH line (they share one 6-wire ribbon; board pinout `LAT /OE CLK DIN GND V+`
  puts CLK one wire from LAT with no ground shield) → a mistimed latch captures a
  partially-shifted frame → bursts of bright channels. **Fix = wiring**, not caps, not a
  slower clock.
- **P1 (separate, still pending):** `docs/adr/0001-default-blank-power-up.md`.
- **The plan + findings:** `docs/diagnostic-plan.org`, `README.org` (`* Findings`),
  `CONTEXT.md` (vocabulary: Chip A/B, blanking, refresh vs update, glitch frame, etc.).
- **PR:** https://github.com/jasonblewis/bbb-clock/pull/1 (branch `p2-stress-harness`).
- **Forum evidence + glitch photos:** `ti-forum/` (gitignored, local only).
- **Project memory** already records this state — read the recalled `p2-diagnostic-state`,
  `refresh-serves-brightness-ramps`, and `bbb-access-and-backup` memories.

## The fix the user is applying
Interleave grounds in the ribbon (grounded **both ends**), the critical one **between CLK
and LAT**. They are trying **grounds first**, and will add **series R (~33–100 Ω) / Schottky
termination on CLK/LAT** only if a residual flicker remains. (See ADR 0002 fix list.)

## Verification procedure (your main task)
The harness `p2stress` is already built at **`/root/p2stress`** on the device (source =
`p2stress.c` in the repo). It has no electrical readback, so **glitch detection is visual —
the user must watch/film the display**; you drive the runs and interpret.

1. Stop the clock so it doesn't fight for the display: `/etc/init.d/clock stop`
2. **Harshest before/after gauge** (was *near-constant* flicker pre-fix):
   `/root/p2stress -a b -p hole -g 100 -s 4000000 -i 100000 -T 300`
   → after a good ribbon fix this should **collapse to little/none**.
3. **SPI-clock sweep** at dim, count flickers/30 s, compare to the pre-fix baselines:

   | SPI clock | Pre-fix flickers / 30 s |
   |---|---|
   | 100 kHz (`-s 100000`) | ~45 |
   | 1 MHz (`-s 1000000`) | ~20 |
   | 4 MHz (`-s 4000000`) | near-constant |

   Command form: `/root/p2stress -a b -p hole -g 100 -s <HZ> -i 100000 -w 1000 -T 300`
4. If flicker persists, advise the user to add the **series R / Schottky termination**
   (ADR 0002) and/or **scope the LAT line** during Arm B to see the ringing directly.
5. When it's confirmed fixed: set **ADR 0002 → Verified/Accepted**, tick the Phase 2/3
   boxes in `docs/diagnostic-plan.org`, update `README.org` findings, and note the result
   on PR #1. Consider whether P1 (ADR 0001) is worth doing while the case is open.

## Device access + operational gotchas (learned the hard way)
- SSH host is **`root@clock.local`** (the `bbb` alias in old notes does NOT resolve).
- Old Debian 7 / gcc 4.6.3 / kernel 3.8.13. Build on-device with
  `gcc -std=gnu99 -O2 -Wall -o /root/p2stress /root/p2stress.c` (no C11; no cross-compile).
- **Deploy the harness in isolated steps** — transfer, verify byte count, THEN build/launch
  separately: `ssh root@clock.local 'cat > /root/p2stress.c; wc -c < /root/p2stress.c' < p2stress.c`
  then a separate ssh to compile. Chaining `kill … && cat > … && gcc … && nohup … &` in one
  command **truncated the file** (undefined reference to main) once — don't.
- **Never `pkill -f "p2stress…"`** — the pattern matches your own remote shell and kills the
  ssh session (exit 255). Kill by explicit PID, captured from `nohup … & echo $!`.
- Launch long runs backgrounded and set a watcher on the PID; the harness auto-stops at `-T`.
  The grayscale latch **holds the last frame even after the process exits** (no visual gap).
- Every P2 run must be **DIM** (`-g 100`–`410`) and drive grayscale directly (the harness
  already bypasses the ambient/brightness path). `temp` reads `n/a` (no thermal zone on 3.8).
- A **whole-FS backup** exists at `/home/jason/bbb-backup-tar.bz2` (verified). The user runs
  backups themselves.
- **Clock is currently running normally** at 1 MHz. Restore after tests with
  `/etc/init.d/clock start`.
- **PRs/GitHub:** `gh` is authenticated locally as `jasonblewis`; `git push` works from this
  machine over SSH. T560 is NOT SSH-reachable from here.

## Nuance worth remembering
The 500 ms display cadence is **not** a pure refresh — it also drives **smooth brightness
ramps** (see the `refresh-serves-brightness-ramps` memory / README "Open decision"). Any
future "update on change" refactor must keep updating while time OR brightness is moving.

## Suggested skills
- **`/diagnosing-bugs`** — resume the hypothesis→experiment→observe loop for the
  verification (feed it ADR 0002 + this doc; the "feedback loop" is `p2stress` + the user's eyes).
- **`/domain-modeling`** — when verified, update ADR 0002 status and sharpen any new terms
  into `CONTEXT.md`.
- **`/verify`** — if you want a structured before/after confirmation of the behaviour change.
