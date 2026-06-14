#!/bin/sh
#
# run_flycast.sh -- launch quake.cdi in Flycast (if installed).
#
# Usage:
#   ./scripts/dc/run_flycast.sh [path/to/quake.cdi]
#
# Flycast is not bundled; install it from https://github.com/flyinghead/flycast
# or your distro package manager.  Attach a VMU to port 1 slot A for saves.
#
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
CDI=${1:-$ROOT/quake.cdi}

if [ ! -f "$CDI" ]; then
    echo "error: $CDI not found. Build with scripts/dc/build_disc.sh first." >&2
    exit 1
fi

FLYCAST=
for c in flycast Flycast flycast-emulator; do
    if command -v "$c" >/dev/null 2>&1; then
	FLYCAST=$c
	break
    fi
done

if [ -z "$FLYCAST" ]; then
    echo "error: Flycast not found on PATH." >&2
    echo "  Install from https://github.com/flyinghead/flycast and retry." >&2
    exit 1
fi

echo "Launching $CDI with $FLYCAST"
exec "$FLYCAST" "$CDI"
