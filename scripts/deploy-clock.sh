#!/bin/bash
# Build clock.c ON the BeagleBone and install it. Never cross-compile: the
# device's gcc is the reference toolchain (see CLAUDE.md / project memory).
#
# Ships clock.c, Makefile and a VERSION file (git describe --always --dirty)
# to /root/bbb-build, builds there, installs to /usr/local/bin/clock keeping
# the previous binary as clock.prev, and prints the version the new binary
# reports. Pass --restart to also restart clock.service.
#
#   scripts/deploy-clock.sh            # build + install only
#   scripts/deploy-clock.sh --restart  # ... and restart the service
#   CLOCK_HOST=root@192.168.7.2 scripts/deploy-clock.sh   # over USB gadget
set -euo pipefail
HOST=${CLOCK_HOST:-root@clock.local}
BUILD=/root/bbb-build
cd "$(dirname "$0")/.."

ver=$(git describe --always --dirty)
case $ver in *-dirty) echo "note: working tree has uncommitted changes; version will be $ver" >&2;; esac
printf '%s\n' "$ver" > VERSION

scp -q clock.c Makefile VERSION "$HOST:$BUILD/"
ssh "$HOST" "set -e; cd $BUILD && make -B clock >/dev/null && ./clock -V \
  && install -m 755 clock /usr/local/bin/clock.new \
  && { [ -e /usr/local/bin/clock ] && mv /usr/local/bin/clock /usr/local/bin/clock.prev || true; } \
  && mv /usr/local/bin/clock.new /usr/local/bin/clock"

if [[ ${1:-} == --restart ]]; then
  ssh "$HOST" "systemctl restart clock.service && sleep 2 && systemctl is-active clock.service && cat /run/clock/version"
fi
