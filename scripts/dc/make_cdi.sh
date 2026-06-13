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
# The data track must contain the game data: id1/pak0.pak (shareware) and,
# for the full game, id1/pak1.pak.  /cd is read-only at runtime, which is why
# sys_dc.c sets basedir to "/cd".
#
set -e

ELF=${1:-quake.elf}
OUT=${2:-quake.cdi}
CD_ROOT=cd_root
VOLNAME=QUAKE

if [ -z "$KOS_BASE" ]; then
    echo "error: KOS_BASE not set -- source the KallistiOS environment first." >&2
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "error: $ELF not found -- run 'make -f Makefile.dreamcast' first." >&2
    exit 1
fi

SCRAMBLE="$KOS_BASE/utils/scramble/scramble"
MAKEIP="$KOS_BASE/utils/makeip/makeip"

# 1) ELF -> raw binary (drop the stack section)
sh-elf-objcopy -R .stack -O binary "$ELF" quake.bin

# 2) Stage the CD filesystem
mkdir -p "$CD_ROOT"
# 2a) scramble the binary into the bootable 1ST_READ.BIN
"$SCRAMBLE" quake.bin "$CD_ROOT/1ST_READ.BIN"

# 2b) game data -- expects ./id1/pak0.pak next to this build
if [ -d id1 ]; then
    cp -r id1 "$CD_ROOT/id1"
else
    echo "warning: ./id1 not found; the disc will boot but find no game data." >&2
fi

# 3) Boot metadata (IP.BIN).  Uses makeip's built-in template; edit a custom
#    IP.tmpl here if you want a custom title/region string.
"$MAKEIP" IP.BIN 2>/dev/null || "$MAKEIP" -v 1.000 -g "QUAKE" IP.BIN

# 4) ISO9660 data track with the bootstrap injected, then convert to CDI
#    -C 0,11702 is the standard MIL-CD lead-in/session offset for DC images.
genisoimage -C 0,11702 -V "$VOLNAME" -G IP.BIN -joliet -rock \
    -o quake.iso "$CD_ROOT"

cdi4dc quake.iso "$OUT"

echo "Built $OUT"
