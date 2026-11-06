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
WS="$(cd "$REPO/.." && pwd)"
OUT="${1:-$WS/build/zephyr-patched}"

rm -rf "$OUT" && mkdir -p "$OUT"
tar -C "$WS/zephyr" --exclude=.git -cf - . | tar -C "$OUT" -xf -
for p in "$REPO"/patches/zephyr/*.patch; do
  echo "applying $(basename "$p")"
  patch -d "$OUT" -p1 -s < "$p"
done
echo "patched copy: $OUT  (base $(git -C "$WS/zephyr" rev-parse --short HEAD))"
