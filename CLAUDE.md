# CLAUDE.md

Guidance for agents working in this repository.

## AI assistance baseline

AI assistance (Claude Code) began on 2026-07-05. The last commit written without AI
assistance is `e649669` ("more changes to make it compile with c17", 2022-06-17).
Every commit from this one onward may have been AI-assisted.

## Agent skills

### Issue tracker

Issues and PRDs live as GitHub issues on `jasonblewis/bbb-clock` (via the `gh` CLI). External PRs **are** a triage surface. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout (`CONTEXT.md` + `docs/adr/` at the repo root). See `docs/agents/domain.md`.
