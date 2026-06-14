#!/bin/sh
#
# build_disc.sh -- build a playable quake.cdi with shareware/full game data.
#
# The Quake pak files are not redistributable, so this script expects you to
# supply them locally.  Either:
#   1) copy id1/pak0.pak into the repo root before running, or
#   2) set QUAKE_ID1 to a directory that contains pak0.pak (and pak1.pak etc.)
#
# Run from a KallistiOS / DreamSDK environment (kos-cc on PATH, KOS_BASE set),
# or let CI build the ELF and run only the packaging half on a machine with the
# KOS disc tools.
#
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

if [ -n "$QUAKE_ID1" ]; then
    echo "Staging game data from QUAKE_ID1=$QUAKE_ID1"
    rm -rf id1
    cp -a "$QUAKE_ID1" id1
fi

if [ ! -f id1/pak0.pak ]; then
    echo "error: id1/pak0.pak not found." >&2
    echo "  Copy shareware id1 next to this tree, or set QUAKE_ID1=/path/to/id1" >&2
    exit 1
fi

echo "=== building quake.elf ==="
make -f Makefile.dreamcast

echo "=== packaging bootable disc ==="
sh ./scripts/dc/make_cdi.sh quake.elf quake.cdi

echo "=== done ==="
ls -lh quake.elf quake.cdi cd_root/1ST_READ.BIN IP.BIN 2>/dev/null || true
echo "Load quake.cdi in Flycast/Redream (attach a VMU in slot A1 for saves)."
