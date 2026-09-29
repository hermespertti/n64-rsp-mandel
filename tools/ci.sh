#!/usr/bin/env bash
# ci.sh — regression gate for the RSP mandel ucode.
# Builds the ROM, runs it in the headless ares-test runner, and FAILS if:
#   - the build breaks
#   - any [probe] line reports mism != 0
#   - pixel-level magenta (verify paint) appears in the screenshot
# Usage: tools/ci.sh [--frames N]   (default 120 frames of tour coverage)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAND="$ROOT/mandel"
ARES_TEST="${ARES_TEST:-$ROOT/docs/ares64/build_headless/test-runner/ares-test}"
FRAMES=120
while [ $# -gt 0 ]; do
    case "$1" in
        --frames) FRAMES="$2"; shift 2 ;;
        *) shift ;;
    esac
done

step() { printf '\n=== %s ===\n' "$*"; }

step "build"
cd "$MAND"
make -s >/dev/null
ls -la mandel.z64

step "headless run ($FRAMES frames)"
mkdir -p test/ci
timeout $((FRAMES * 2 + 120)) "$ARES_TEST" test/ci.js mandel.z64 "$FRAMES" \
    --timeout $((FRAMES * 2 + 60)) > test/ci/run.log 2>&1 || {
        echo "FAIL: ares-test exited nonzero"; tail -20 test/ci/run.log; exit 1; }

step "verdict"
probes=$(grep -ac '\[probe\]' test/ci/run.log || true)
bad=$(grep -a '\[probe\]' test/ci/run.log | grep -avc 'mism=0' || true)
echo "probe lines: $probes  nonzero-mismatch: $bad"

if [ "$probes" -lt 1 ]; then
    echo "FAIL: no probe output (ISV channel dead?)"; tail -20 test/ci/run.log; exit 1
fi
if [ "$bad" -ne 0 ]; then
    echo "FAIL: ucode mismatches vs CPU reference:"
    grep -a '\[probe\]' test/ci/run.log | grep -av 'mism=0' | head -5
    exit 1
fi

# pixel-level verify: stage-C/D painted mismatches bright magenta;
# ci.js also reports its own independent shot decode
if grep -aq 'PIC mismatch=[1-9]' test/ci/run.log; then
    echo "FAIL: magenta verify pixels in screenshot"
    grep -a 'PIC' test/ci/run.log | head -3
    exit 1
fi

if ! grep -aq 'CI_PASS' test/ci/run.log; then
    echo "FAIL: runner did not report CI_PASS"
    tail -10 test/ci/run.log; exit 1
fi

echo "PASS: $probes probe windows, mism=0, no magenta verify pixels"
