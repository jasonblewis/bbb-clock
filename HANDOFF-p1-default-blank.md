# Handoff — P1: make "blanked" the fail-safe power-up default

## What this is
Start work on **P1 / power-up garbage**: at cold boot the display shows random bright
LEDs until software writes valid data. The fix is designed but **not yet applied or
verified**. P2 (the runtime glitch) is **done and closed** — P1 is fully independent of it.

**Everything technical is already written in `docs/adr/0001-default-blank-power-up.md`**
(mechanism, the BC548/gpio20 blanking circuit, the decision, the verification steps, and
the open items). Read that first — this handoff does **not** repeat it; it only tells you
where things stand and what to actually do next.

## Where to work
- **Main checkout on `master`** — `/home/jason/nextcloud/projects/bbb-clock`. All P1 files
  are on master now (P2 was merged in `16fdac0`; master is at `cf4491b`, pushed to origin).
  **Do NOT use the old `.claude/worktrees/p2-stress-harness` worktree** — that was the P2
  branch and is now merged; it's safe to `git worktree remove` if it's in the way.
- P1 files: `docs/adr/0001-default-blank-power-up.md`, the draft overlay
  `BB-BLANK-00A0.dts`, Phase 4 checklist in `docs/diagnostic-plan.org`, blanking scripts
  under `displayon/` (`displayon.sh` / `displayoff.sh` drive gpio20).

## The critical framing — split the work by who can do it
The **load-bearing fix is a soldering change** (ADR 0001 §Decision item 1): one ~10 kΩ
resistor from the **BC548 base to GND**. That is **the user's hands** — an agent cannot do
it, and it alone resolves P1. So the agent's real work is the desk/verification work
*around* it:

1. **[Agent, no device needed — the concrete blocker] Verify the DT overlay register
   offsets.** `BB-BLANK-00A0.dts` has its P9_41 / gpio0_20 pad control-register offsets
   **flagged as unverified** (see the file's comments and ADR 0001 §Open items). Confirm
   them against the **AM335x TRM** (control-module conf_ register map) and the **BBB SRM**
   (P9_41 pad name + the P9_41 "double-pin" sibling gpio3_20/gpio116) before anyone
   compiles/flashes it. This is primary-source doc research — a good fit for `/research`.
2. **[Agent + user] Decide overlay scope.** Recommendation already in ADR 0001: the base
   pull-down alone is sufficient; treat the early-boot pinmux overlay as optional
   defense-in-depth. Confirm the user agrees before investing in wiring the overlay into
   the early boot path (U-Boot / base DTB / `uEnv.txt` — **not** `rc.local`, which is too
   late; see ADR 0001 §"Why software/boot-order tweaks are not enough").
3. **[User solders, agent drives verification] Post-change cold-boot check.** Run ADR 0001
   §Verification: full power-cycle with display connected → expect **no lit LEDs** at any
   point through boot until `clock` starts; then confirm normal operation and that
   `displayon.sh`/`displayoff.sh` still toggle. **The user must do the physical
   power-cycle and watch the display** (same visual-detection setup as P2).
4. **[Agent] Close the docs** once verified: set ADR 0001 → **Accepted**, tick the Phase 4
   boxes in `docs/diagnostic-plan.org`, update `README.org` findings, commit. (P2's
   sign-off in commit `355312e` is the template to mirror.)

## Device access + operational notes
- SSH host is **`root@clock.local`** (the `bbb` alias in old notes does not resolve).
- Old Debian 7 / gcc 4.6.3 / kernel 3.8.13. gpio state readback:
  `cat /sys/kernel/debug/gpio` (look for `gpio-20 ... out hi` = display enabled).
- The clock daemon is `/usr/local/bin/clock -t`, managed by `/etc/init.d/clock
  {start|stop}` (no `status` verb). It is **running normally now** — P1 verification needs
  a **cold power cycle**, which interrupts it; coordinate with the user.
- A whole-FS backup exists at `/home/jason/bbb-backup-tar.bz2` (verified). The user runs
  backups themselves. See the `bbb-access-and-backup` project memory.
- `gh` is authenticated locally as `jasonblewis`; `git push` works over SSH from this
  machine. Issues/PRDs live as GitHub issues on `jasonblewis/bbb-clock`.

## State to be aware of
- P1 has **no diagnostic loop left to run** — the cause is understood (ADR 0001). This is
  an *apply-and-verify* task, gated on the user's soldering, not a `/diagnosing-bugs` hunt.
- Vocabulary (Chip A/B, blanking, /OE, refresh vs update) is in `CONTEXT.md`.
- Project memory already tracks this: `p2-diagnostic-state` (P2 done + P1 open),
  `bbb-access-and-backup`, `refresh-serves-brightness-ramps`.

## Suggested skills
- **`/research`** — verify the `BB-BLANK-00A0.dts` pad-control-register offsets against the
  **AM335x TRM** and **BBB SRM** (primary sources). This is the main agent-doable blocker
  and captures the finding as a repo Markdown file.
- **`/verify`** — structured before/after confirmation of the cold-boot behaviour change
  once the resistor is in (drive the power-cycle observation with the user).
- **`/domain-modeling`** — when verified, flip ADR 0001 → Accepted and sharpen any new
  terms into `CONTEXT.md` (same closeout shape used for ADR 0002).
