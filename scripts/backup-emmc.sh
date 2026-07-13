#!/usr/bin/env bash
#
# backup-emmc.sh — pull a full, bit-for-bit image of the BeagleBone Black's
# eMMC over SSH, compressing on the client (your machine, not the BBB).
#
# This is the *bare-metal / disaster-recovery* backup: it captures the whole
# block device — partition table, u-boot/MLO, and root filesystem — so the
# resulting image can be flashed to a fresh eMMC or SD card and boot as-is.
# For lightweight, incremental config/data snapshots use restic instead.
#
# Design notes:
#   - dd runs on the BBB and reads the raw block device; the AM335x is weak,
#     so *compression happens locally* via zstd -T0 (all cores). This is the
#     one call the original bzip2-over-ssh post got right; we just modernise
#     the compressor.
#   - pv shows throughput + ETA (we fetch the device size up front for -s).
#   - The image is checksummed and integrity-tested before we call it done.
#
# LIVE-BACKUP CAVEAT: this images a *mounted, running* root filesystem. For a
# mostly-idle clock the risk of a torn image is low, and we `sync` first, but
# the only way to a guaranteed-consistent image is to boot the BBB from an SD
# card so the eMMC is unmounted, then image /dev/mmcblk1. See --help.
#
# Usage:
#   ./scripts/backup-emmc.sh [output-dir]
#
# Environment overrides:
#   BBB_HOST     ssh target                (default: root@clock.local)
#   BBB_DEVICE   block device to image     (default: auto — the disk backing /)
#   ZSTD_LEVEL   compression level 1-19    (default: 12)
#   OUT_DIR      output directory          (default: . or $1)
#
# NOTE on device numbering: on this clock the 4GB eMMC is /dev/mmcblk0 (not
# mmcblk1 as on many BBBs). Rather than hardcode either, we auto-detect the
# disk that backs the running root filesystem, so the image is always of the
# right device. Override with BBB_DEVICE=/dev/... if you need a specific one.

set -euo pipefail

BBB_HOST="${BBB_HOST:-root@clock.local}"
BBB_DEVICE="${BBB_DEVICE:-}"
ZSTD_LEVEL="${ZSTD_LEVEL:-12}"
OUT_DIR="${OUT_DIR:-${1:-.}}"

# Fixed timestamp for a stable, sortable filename. We ask the shell once so
# the .img.zst and its .sha256 sidecar share the same stamp.
STAMP="$(date +%Y%m%d-%H%M%S)"
BASENAME="bbb-emmc-${STAMP}.img.zst"
OUT_FILE="${OUT_DIR%/}/${BASENAME}"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33m[!]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[x]\033[0m %s\n' "$*" >&2; exit 1; }

case "${1:-}" in
  -h|--help)
    sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
esac

# --- Preflight -------------------------------------------------------------

for tool in ssh pv zstd; do
  command -v "$tool" >/dev/null 2>&1 || die "missing local tool: $tool"
done

[ -d "$OUT_DIR" ] || die "output dir does not exist: $OUT_DIR"

log "Checking SSH reachability of ${BBB_HOST} ..."
ssh -o ConnectTimeout=10 -o BatchMode=yes "$BBB_HOST" true 2>/dev/null \
  || die "cannot ssh to ${BBB_HOST} (need key auth; try: ssh ${BBB_HOST})"

# If the caller didn't pin a device, image the disk that backs the running
# root filesystem. This avoids the mmcblk0-vs-mmcblk1 footgun: on this clock
# the eMMC is mmcblk0, on other BBBs it's mmcblk1 — either way, "the disk / is
# on" is always the right answer.
if [ -z "$BBB_DEVICE" ]; then
  log "Auto-detecting the disk that backs / on ${BBB_HOST} ..."
  ROOT_SRC="$(ssh "$BBB_HOST" \
    "findmnt -no SOURCE / 2>/dev/null || awk '\$2==\"/\"{print \$1}' /proc/mounts" \
    | head -n1)"
  [ -n "$ROOT_SRC" ] || die "could not determine root device on ${BBB_HOST}"
  # Strip the partition suffix: mmcblk0p2 -> mmcblk0 ; sda1 -> sda
  case "$ROOT_SRC" in
    *mmcblk*|*nvme*) BBB_DEVICE="${ROOT_SRC%p[0-9]*}" ;;
    *)               BBB_DEVICE="$(printf '%s' "$ROOT_SRC" | sed 's/[0-9]*$//')" ;;
  esac
  log "  root is on ${ROOT_SRC} -> will image whole disk ${BBB_DEVICE}"
fi

log "Confirming ${BBB_DEVICE} exists on the device ..."
ssh "$BBB_HOST" "test -b ${BBB_DEVICE}" \
  || die "${BBB_DEVICE} is not a block device on ${BBB_HOST}"

# Size drives pv's ETA and lets us sanity-check local free space.
DEV_BYTES="$(ssh "$BBB_HOST" "blockdev --getsize64 ${BBB_DEVICE}")"
[ "$DEV_BYTES" -gt 0 ] 2>/dev/null || die "could not read device size"
DEV_HUMAN="$(numfmt --to=iec --suffix=B "$DEV_BYTES" 2>/dev/null || echo "${DEV_BYTES}B")"

# Worst case the compressed image approaches the raw size; warn if free space
# on the target dir is below the *uncompressed* device size.
FREE_BYTES="$(df -P --block-size=1 "$OUT_DIR" | awk 'NR==2{print $4}')"
if [ "$FREE_BYTES" -lt "$DEV_BYTES" ]; then
  warn "Free space in ${OUT_DIR} ($(numfmt --to=iec --suffix=B "$FREE_BYTES")) is"
  warn "less than the raw device size (${DEV_HUMAN}). Compression will very"
  warn "likely fit, but if the eMMC is nearly full this could run out of room."
fi

# --- Confirm (live backup) -------------------------------------------------

warn "About to image a MOUNTED, RUNNING root filesystem (${BBB_DEVICE}, ${DEV_HUMAN})."
warn "For a guaranteed-consistent image, boot the BBB from SD and image the"
warn "unmounted eMMC instead. Proceeding gives a 'good enough' live snapshot."
printf 'Continue? [y/N] ' >&2
read -r reply
case "$reply" in
  y|Y|yes|YES) ;;
  *) die "aborted by user" ;;
esac

# --- Backup ----------------------------------------------------------------

# Clean up a partial image if anything below fails.
cleanup() {
  if [ "${DONE:-0}" != 1 ] && [ -f "$OUT_FILE" ]; then
    warn "removing partial image ${OUT_FILE}"
    rm -f "$OUT_FILE"
  fi
}
trap cleanup EXIT

log "Flushing device buffers (sync) ..."
ssh "$BBB_HOST" "sync"

log "Imaging ${BBB_DEVICE} -> ${OUT_FILE}"
log "  host=${BBB_HOST} size=${DEV_HUMAN} zstd=-${ZSTD_LEVEL} threads=all"

# dd reads the raw block device on the BBB; pv meters the stream locally with a
# known total size for ETA; zstd compresses on the client using every core.
# `set -o pipefail` (above) makes a failure in dd/ssh fail the whole pipeline.
# pv meters the stream locally with a known total size for ETA. In an
# interactive terminal it updates a single line in place; when stderr isn't a
# real TTY (e.g. inside a multiplexer/capture pane) pv prints one line per
# refresh instead, so we lengthen the interval to keep that readable. Override
# with PV_INTERVAL, or set NO_PV=1 to drop pv entirely.
PV_INTERVAL="${PV_INTERVAL:-5}"
# Custom pv format focused on "how much longer":
#   %p progress bar/%   %b transferred   %a AVERAGE rate (stable, so ETA is
#   trustworthy)   %e time remaining   %I estimated finish (wall clock).
# ETA reflects the whole raw device — dd images free space too — so it's an
# honest estimate of the real run, not just the used portion.
# (pv auto-labels %e as "ETA ..." and %I as "FIN <clock time>", so we don't
#  add our own "remaining"/"finish" words.)
PV_FORMAT="${PV_FORMAT:-imaging %p %b at %a avg, %e, %I}"
if [ "${NO_PV:-0}" = 1 ]; then
  ssh "$BBB_HOST" "dd if=${BBB_DEVICE} bs=4M status=none" \
    | zstd -T0 "-${ZSTD_LEVEL}" --long=27 -o "$OUT_FILE"
else
  ssh "$BBB_HOST" "dd if=${BBB_DEVICE} bs=4M status=none" \
    | pv -s "$DEV_BYTES" -i "$PV_INTERVAL" -F "$PV_FORMAT" \
    | zstd -T0 "-${ZSTD_LEVEL}" --long=27 -o "$OUT_FILE"
fi

# --- Verify ----------------------------------------------------------------

log "Verifying compressed image integrity (zstd -t) ..."
zstd -t "$OUT_FILE"

log "Writing SHA-256 checksum ..."
( cd "$OUT_DIR" && sha256sum "$BASENAME" > "${BASENAME}.sha256" )

DONE=1
COMP_HUMAN="$(numfmt --to=iec --suffix=B "$(stat -c%s "$OUT_FILE")" 2>/dev/null || echo '?')"

log "Done."
cat >&2 <<EOF

  Image:     ${OUT_FILE}   (${COMP_HUMAN} compressed, ${DEV_HUMAN} raw)
  Checksum:  ${OUT_FILE}.sha256

  Restore to a fresh SD card or eMMC (DOUBLE-CHECK the target device!):

    zstd -dc "${OUT_FILE}" | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync

  Verify a copy later with:

    ( cd "${OUT_DIR%/}" && sha256sum -c "${BASENAME}.sha256" )
EOF
