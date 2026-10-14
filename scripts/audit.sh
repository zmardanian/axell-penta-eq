#!/usr/bin/env bash
# Independent fidelity audit (tests/independent/README.md): builds PentaAuditHost inside the distrobox, then runs
# the Python audits on the host (numpy + scipy) against the built plugin bundle, or the one given as $1.
# Writes tests/independent/audit-report.txt. Exit code = number of failures.
set -uo pipefail
cd "$(dirname "$0")/.."
DIR="$PWD"
distrobox enter juce-build -- bash -lc "cd '$DIR' && { [ -f build/CMakeCache.txt ] || cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release; } && cmake --build build --target PentaAuditHost -j\$(nproc)" >/dev/null || exit 1
export PENTA_VST3="${1:-$DIR/build/AxellPentaEQ_artefacts/Release/VST3/Axell Penta EQ.vst3}"
REPORT=tests/independent/audit-report.txt
{
  echo "Axell Penta EQ - independent audit, $(date -u +%Y-%m-%d), plugin: ${PENTA_VST3#$DIR/}"
  python3 tests/independent/audit_mag.py; a=$?
  echo
  python3 tests/independent/audit_misc.py; b=$?
  echo
  echo "TOTAL FAILURES: $((a + b))"
  exit $((a + b))
} 2>&1 | tee "$REPORT"
exit "${PIPESTATUS[0]}"
