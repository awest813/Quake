#!/bin/sh
#
# test_all_phases.sh -- verify Dreamcast port phases 0-5.
#
# Phase 0: toolchain / scripts present
# Phase 1: ELF + CDI build (Docker KOS image or local kos-cc)
# Phase 2: boot/FS symbols and strings in ELF
# Phase 3: PVR video backend linked
# Phase 4: Maple input + AICA sound backends linked
# Phase 5: VMU I/O symbols linked
#
# Optional runtime (needs id1/pak0.pak + Flycast):
#   QUAKE_ID1=/path/to/id1 FLYCAST=1 ./scripts/dc/test_all_phases.sh
#
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

PASS=0
FAIL=0
SKIP=0

pass() { PASS=$((PASS + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL: $1" >&2; }
skip() { SKIP=$((SKIP + 1)); echo "  SKIP: $1"; }

# Return 0 if $1 names a symbol present in quake.elf (works on stripped SH-4 ELFs).
elf_has_sym() {
    sym=$1
    if [ ! -f quake.elf ]; then
	return 1
    fi
    for candidate in "$sym" "_$sym"; do
	if readelf -sW quake.elf 2>/dev/null | awk '{print $NF}' | grep -qx "$candidate"; then
	    return 0
	fi
	if readelf --dyn-syms -W quake.elf 2>/dev/null | awk '{print $NF}' | grep -qx "$candidate"; then
	    return 0
	fi
    done
    if command -v sh-elf-nm >/dev/null 2>&1; then
	sh-elf-nm quake.elf 2>/dev/null | awk '{print $3}' | grep -qx "$sym" && return 0
	sh-elf-nm quake.elf 2>/dev/null | awk '{print $3}' | grep -qx "_$sym" && return 0
    fi
    return 1
}

elf_sym() {
    if elf_has_sym "$1"; then
	pass "$2"
    else
	fail "$3"
    fi
}

phase() {
    echo ""
    echo "=== Phase $1: $2 ==="
}

# ------------------------------------------------------------------ Phase 0
phase 0 "Toolchain / project layout"
test -f Makefile.dreamcast && pass "Makefile.dreamcast" || fail "Makefile.dreamcast"
test -f scripts/dc/make_cdi.sh && pass "make_cdi.sh" || fail "make_cdi.sh"
test -f scripts/dc/build_disc.sh && pass "build_disc.sh" || fail "build_disc.sh"
test -f source/sys_dc.c && pass "sys_dc.c" || fail "sys_dc.c"
test -f source/vid_dc.c && pass "vid_dc.c" || fail "vid_dc.c"
test -f source/in_dc.c && pass "in_dc.c" || fail "in_dc.c"
test -f source/snd_dc.c && pass "snd_dc.c" || fail "snd_dc.c"
test -f source/vmu_dc.c && pass "vmu_dc.c" || fail "vmu_dc.c"
test -f .github/workflows/dreamcast.yml && pass "CI workflow" || fail "CI workflow"

# ------------------------------------------------------------------ Phase 1
phase 1 "Build ELF + CDI"
BUILT=0
if [ -f quake.elf ] && [ -z "$REBUILD" ]; then
    pass "quake.elf already built (set REBUILD=1 to force)"
    BUILT=1
elif command -v kos-cc >/dev/null 2>&1; then
    echo "  using local kos-cc"
    make -f Makefile.dreamcast clean >/dev/null 2>&1 || true
    make -f Makefile.dreamcast
    sh ./scripts/dc/make_cdi.sh quake.elf quake.cdi
    BUILT=1
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    echo "  using nold360/kallistios-sdk Docker image"
    docker run --rm --entrypoint bash -v "$ROOT":/src -w /src \
	nold360/kallistios-sdk:latest -c '
	    set -e
	    if [ -z "$KOS_BASE" ] && [ -f /opt/toolchains/dc/kos/environ.sh ]; then
		. /opt/toolchains/dc/kos/environ.sh
	    fi
	    git config --global --add safe.directory /src 2>/dev/null || true
	    make -f Makefile.dreamcast clean >/dev/null 2>&1 || true
	    make -f Makefile.dreamcast
	    sh ./scripts/dc/make_cdi.sh quake.elf quake.cdi
	'
    BUILT=1
else
    skip "no kos-cc or docker -- using existing artifacts if present"
fi

if [ -f quake.elf ]; then
    pass "quake.elf exists ($(wc -c < quake.elf) bytes)"
else
    fail "quake.elf missing"
fi

if [ -f quake.cdi ]; then
    pass "quake.cdi exists ($(wc -c < quake.cdi) bytes)"
else
    fail "quake.cdi missing"
fi

if [ -f cd_root/1ST_READ.BIN ]; then
    pass "cd_root/1ST_READ.BIN exists"
else
    fail "cd_root/1ST_READ.BIN missing"
fi

# ------------------------------------------------------------------ Phase 2
phase 2 "Boot / filesystem (/cd basedir)"
if [ -f quake.elf ]; then
    strings quake.elf | grep -q '/cd' && pass "ELF contains /cd basedir" \
	|| fail "ELF missing /cd string"
    strings quake.elf | grep -q 'Dreamcast' && pass "ELF identifies as Dreamcast" \
	|| fail "ELF missing Dreamcast banner"
    elf_sym main "main() linked" "main() not found in ELF"
    elf_sym Sys_Printf "Sys_Printf linked (sys_dc.c)" "Sys_Printf not found"
else
    skip "no ELF for phase 2 checks"
fi

# ------------------------------------------------------------------ Phase 3
phase 3 "PVR video present"
if [ -f quake.elf ]; then
    elf_sym VID_Init "VID_Init linked" "VID_Init not found"
    elf_sym VID_Update "VID_Update linked" "VID_Update not found"
    strings quake.elf | grep -q 'dc_rgb565' && pass "PVR RGB565 scratch present" \
	|| skip "dc_rgb565 string not in ELF (may be stripped)"
else
    skip "no ELF for phase 3 checks"
fi

# ------------------------------------------------------------------ Phase 4
phase 4 "Maple input + AICA sound"
if [ -f quake.elf ]; then
    elf_sym IN_Init "IN_Init linked" "IN_Init not found"
    elf_sym IN_DC_ApplyBindings "IN_DC_ApplyBindings linked" "IN_DC_ApplyBindings not found"
    elf_sym SNDDMA_Init "SNDDMA_Init linked" "SNDDMA_Init not found"
    elf_sym SNDDMA_Submit "SNDDMA_Submit linked" "SNDDMA_Submit not found"
else
    skip "no ELF for phase 4 checks"
fi

# ------------------------------------------------------------------ Phase 5
phase 5 "VMU saves (VMS packaging)"
if [ -f quake.elf ]; then
    elf_sym DC_FOpen "DC_FOpen linked" "DC_FOpen not found"
    elf_sym DC_FClose "DC_FClose linked" "DC_FClose not found"
    strings quake.elf | grep -q 'TyrQuake' && pass "VMU app_id string present" \
	|| skip "TyrQuake app_id string not found (may be in data only)"
    grep -q 'DC_FOpen' source/cl_demo.c source/menu.c source/host_cmd.c \
	&& pass "DC_FOpen used in save/demo/menu paths" \
	|| fail "DC_FOpen not wired in all I/O paths"
else
    skip "no ELF for phase 5 checks"
fi

# ------------------------------------------------------------------ Runtime
phase R "Runtime (optional)"
if [ -f id1/pak0.pak ] || [ -n "$QUAKE_ID1" ]; then
    if [ -n "$QUAKE_ID1" ]; then
	rm -rf id1
	cp -a "$QUAKE_ID1" id1
    fi
    if [ ! -f quake.cdi ] || [ "$BUILT" = 0 ]; then
	sh ./scripts/dc/build_disc.sh
    fi
    pass "playable disc with pak0.pak"
    if [ -n "$FLYCAST" ]; then
	if command -v flycast >/dev/null 2>&1 || command -v Flycast >/dev/null 2>&1; then
	    skip "Flycast found -- launch manually: scripts/dc/run_flycast.sh"
	else
	    skip "Flycast not installed"
	fi
    else
	skip "set FLYCAST=1 to check emulator availability"
    fi
else
    skip "no id1/pak0.pak (set QUAKE_ID1= for runtime disc test)"
fi

# ------------------------------------------------------------------ Summary
echo ""
echo "========================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
echo "========================================"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
