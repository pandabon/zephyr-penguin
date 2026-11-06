#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Make a COPY of the workspace's zephyr/ tree with patches/zephyr/*.patch
# applied, for testing the proposed fork patches without touching zephyr/.
#
# Usage: scripts/make_patched_zephyr.sh [OUT_DIR]   (default: ../build/zephyr-patched)
# Then e.g.: ZEPHYR_BASE=<OUT_DIR> west build ...
#        or: PATCHED_ZEPHYR=<OUT_DIR> the bench bundle builder (outside this repo)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
# Find the Zephyr tree to copy: $ZEPHYR_BASE if set, else the west workspace's zephyr/, else a
# sibling of this repo. (Taking the workspace from this repo's parent folder alone fails when the
# repo is a git worktree outside the workspace folder.)
if [ -n "${ZEPHYR_BASE:-}" ] && [ -d "$ZEPHYR_BASE" ]; then
  ZEPHYR="$ZEPHYR_BASE"
elif command -v west >/dev/null 2>&1 && T="$(cd "$REPO" && west topdir 2>/dev/null)" && [ -d "$T/zephyr" ]; then
  ZEPHYR="$T/zephyr"
else
  ZEPHYR="$REPO/../zephyr"
fi
[ -d "$ZEPHYR/kernel" ] || { echo "error: no Zephyr tree found (set ZEPHYR_BASE); tried $ZEPHYR" >&2; exit 2; }
ZEPHYR="$(cd "$ZEPHYR" && pwd)"
WS="$(dirname "$ZEPHYR")"
OUT="${1:-$WS/build/zephyr-patched}"
case "$(cd "$(dirname "$OUT")" 2>/dev/null && pwd)/$(basename "$OUT")" in
  "$ZEPHYR"|"$ZEPHYR"/*|"$REPO"|"$REPO"/*) echo "error: OUT_DIR must not be inside zephyr/ or this repo: $OUT" >&2; exit 2;;
esac

rm -rf "$OUT" && mkdir -p "$OUT"
tar -C "$ZEPHYR" --exclude=.git -cf - . | tar -C "$OUT" -xf -
for p in "$REPO"/patches/zephyr/*.patch; do
  echo "applying $(basename "$p")"
  patch -d "$OUT" -p1 -s < "$p"
done
echo "patched copy: $OUT  (base $(git -C "$ZEPHYR" rev-parse --short HEAD))"
