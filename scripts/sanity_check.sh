#!/usr/bin/env bash
# scripts/sanity_check.sh — static analysis: clang-tidy (.clang-tidy) + cppcheck.
#
# Usage: make sanity-check   (CLANG_TIDY=/path/to/clang-tidy CPPCHECK=... to override)
# Needs build/compile_commands.json (make build). Report: stdout and logs/sanity/last.txt.
# Findings are a report, not a gate: the exit code is 0 unless a tool itself fails to run.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT_DIR="$ROOT/logs/sanity"
OUT_FILE="$OUT_DIR/last.txt"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
CPPCHECK="${CPPCHECK:-cppcheck}"

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "no $BUILD_DIR/compile_commands.json: run 'make build' first" >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

# Our translation units only: tests are left out (GoogleTest macros drown the report).
mapfile -t SOURCES < <(cd "$ROOT" && ls src/*.cpp src/applog/*.cpp src/cli/*.cpp src/video/*.cpp sim/*.cpp)

status=0
{
    echo "# DJI Tello CLI sanity-check — $(date '+%Y-%m-%d %H:%M')"
    echo

    echo "### clang-tidy"
    if command -v "$CLANG_TIDY" >/dev/null 2>&1; then
        (cd "$ROOT" && "$CLANG_TIDY" -p "$BUILD_DIR" --quiet "${SOURCES[@]}" 2>/dev/null)
        rc=$?
        # clang-tidy exits 1 when it reports warnings; only other codes mean it did not run.
        if [[ $rc -gt 1 ]]; then
            echo "clang-tidy failed with exit code $rc"
            status=1
        fi
    else
        echo "SKIPPED: '$CLANG_TIDY' not found (sudo apt install clang-tidy)"
    fi
    echo

    echo "### cppcheck"
    if command -v "$CPPCHECK" >/dev/null 2>&1; then
        "$CPPCHECK" --project="$BUILD_DIR/compile_commands.json" \
            --enable=warning,performance --inline-suppr --quiet \
            --file-filter="$ROOT/src/*" --file-filter="$ROOT/sim/*" || status=1
    else
        echo "SKIPPED: '$CPPCHECK' not found (sudo apt install cppcheck)"
    fi
} > "$OUT_FILE" 2>&1

{
    echo
    echo "### summary"
    echo "findings: $(grep -cE '(warning|error|style|performance):' "$OUT_FILE")"
} >> "$OUT_FILE"

cat "$OUT_FILE"
echo
echo "Saved to: $OUT_FILE"
exit $status
