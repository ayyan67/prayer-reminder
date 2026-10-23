#!/bin/sh
# Build PrayerReminder.exe with MinGW-w64 (MSYS2 UCRT64 shell or Git Bash
# with gcc on PATH). Links only against Windows system DLLs.
set -e

ROOT=$(cd "$(dirname "$0")" && pwd)
SRC="$ROOT/src"
OUT="$ROOT/PrayerReminder.exe"

command -v gcc >/dev/null 2>&1 || {
    echo "gcc not found. Install MSYS2 and put ucrt64/bin on PATH." >&2
    exit 1
}

echo "Compiling resources..."
windres "$SRC/app.rc" -O coff -o "$ROOT/app.res" --include-dir "$SRC"

echo "Compiling..."
gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -municode -mwindows \
    -o "$OUT" \
    "$SRC/main.c" "$SRC/ui_main.c" "$SRC/ui_toast.c" "$SRC/gfx.c" \
    "$SRC/times.c" "$SRC/sched.c" "$SRC/config.c" "$SRC/util.c" "$SRC/net.c" \
    "$ROOT/app.res" \
    -lwinhttp -ldwmapi -lshell32 -lcomctl32 -lgdi32 -luser32 -lole32 -lwinmm -lm

rm -f "$ROOT/app.res"
echo "Built $OUT"
