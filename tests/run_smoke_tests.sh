#!/usr/bin/env sh
set -eu
BIN="${1:-./build/patterndetect}"
LOG_TMP="$(mktemp -d)"
trap 'rm -rf "$LOG_TMP"' EXIT
expr_out="$("$BIN" --operation-log-prefix "$LOG_TMP/expr" --expr 'add(multiply(2,3),4)')"
[ "$expr_out" = "10" ] || { echo "expression test failed: $expr_out"; exit 1; }
"$BIN" --operation-log-prefix "$LOG_TMP/validation" --validate-only --config examples/patterndetect.conf >/tmp/patterndetect_validate.out
"$BIN" --operation-log-prefix "$LOG_TMP/sample" --config examples/patterndetect.conf --log examples/sample.log >/tmp/patterndetect_scan.out
grep -q "REPORT: Initial problem1 found" /tmp/patterndetect_scan.out
grep -q "REPORT: Problem1 issue marker 2 observed" /tmp/patterndetect_scan.out
echo "PatternDetect smoke tests passed"
