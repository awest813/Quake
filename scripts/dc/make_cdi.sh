#!/bin/sh
#
# make_cdi.sh -- build a bootable Dreamcast .CDI from quake.elf
#
# Run from a DreamSDK / KallistiOS shell after `make -f Makefile.dreamcast`,
# with the KOS environment sourced (KOS_BASE set, tools on PATH).
#
# Pipeline (standard KallistiOS):
#   ELF -> raw binary -> scrambled 1ST_READ.BIN -> ISO9660 (+IP.BIN) -> CDI
#
# Tools are resolved from PATH (the DreamSDK / nold360 SDK images install
# scramble, makeip, genisoimage and cdi4dc into /usr/local/bin), with KOS
# util-dir fallbacks.  The data track must contain the game data:
# id1/pak0.pak (shareware) and, for the full game, id1/pak1.pak.  /cd is
# read-only at runtime, which is why sys_dc.c sets basedir to "/cd".
#
set -e

ELF=${1:-quake.elf}
OUT=${2:-quake.cdi}
CD_ROOT=cd_root
VOLNAME=QUAKE

if [ ! -f "$ELF" ]; then
    echo "error: $ELF not found -- run 'make -f Makefile.dreamcast' first." >&2
    exit 1
fi

# --- resolve tools (PATH first, then KOS util dirs) -------------------------
find_tool() {
    # $1 = tool name, $2.. = extra candidate paths
    name=$1; shift
    if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return 0; fi
    for c in "$@"; do [ -x "$c" ] && { echo "$c"; return 0; }; done
    return 1
}

OBJCOPY=${KOS_OBJCOPY:-$(find_tool sh-elf-objcopy)} || OBJCOPY=$(find_tool objcopy)
SCRAMBLE=$(find_tool scramble "$KOS_BASE/utils/scramble/scramble") || true
MAKEIP=$(find_tool makeip "$KOS_BASE/utils/makeip/makeip") || true
GENISO=$(find_tool genisoimage mkisofs) || true
CDI4DC=$(find_tool cdi4dc) || true

echo "objcopy=$OBJCOPY  scramble=$SCRAMBLE  makeip=$MAKEIP  geniso=$GENISO  cdi4dc=$CDI4DC"

[ -n "$OBJCOPY" ]  || { echo "error: no objcopy found"   >&2; exit 1; }
[ -n "$SCRAMBLE" ] || { echo "error: no scramble found"  >&2; exit 1; }

# 1) ELF -> raw binary (drop the stack section)
"$OBJCOPY" -R .stack -O binary "$ELF" quake.bin

# 2) Stage the CD filesystem with the bootable, scrambled 1ST_READ.BIN
mkdir -p "$CD_ROOT"
"$SCRAMBLE" quake.bin "$CD_ROOT/1ST_READ.BIN"

# 2b) game data -- expects ./id1/pak0.pak next to this build
if [ -d id1 ]; then
    cp -r id1 "$CD_ROOT/id1"
else
    echo "warning: ./id1 not found; the disc will boot but find no game data." >&2
fi

# 3) Boot metadata (IP.BIN).  The DreamSDK makeip generates a homebrew default
#    when given just an output path; fall back to passing a game title.
if [ -n "$MAKEIP" ]; then
    "$MAKEIP" IP.BIN 2>/dev/null \
        || "$MAKEIP" -g "$VOLNAME" IP.BIN 2>/dev/null \
        || { echo "warning: makeip failed; dumping usage:"; "$MAKEIP" --help 2>&1 | head -20 || true; }
fi

# 4) ISO9660 data track (+IP.BIN bootstrap) then convert to CDI.
#    -C 0,11702 is the standard MIL-CD session offset for DC images.
if [ -n "$GENISO" ] && [ -f IP.BIN ]; then
    "$GENISO" -C 0,11702 -V "$VOLNAME" -G IP.BIN -joliet -rock \
        -o quake.iso "$CD_ROOT"
    if [ -n "$CDI4DC" ]; then
        "$CDI4DC" quake.iso "$OUT"
        echo "Built $OUT"
    else
        echo "note: cdi4dc not found; produced quake.iso (no .cdi)."
    fi
else
    echo "note: genisoimage/IP.BIN unavailable; produced $CD_ROOT/1ST_READ.BIN only."
fi
