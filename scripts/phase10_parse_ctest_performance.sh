#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <ctest-log-file>"
  exit 1
fi

LOG_FILE="$1"

if [ ! -f "$LOG_FILE" ]; then
  echo "Log file not found: $LOG_FILE" >&2
  exit 1
fi

trim() {
  local v="$1"
  v="${v#"${v%%[![:space:]]*}"}"
  v="${v%"${v##*[![:space:]]}"}"
  echo "$v"
}

overall_line=$(grep -E '^[[:space:]]*[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "$LOG_FILE" | head -n 1 || true)
if [ -n "$overall_line" ]; then
  echo "OVERALL_RESULT=$(trim "$overall_line")"
fi

awk '
function trim_local(str) {
  sub(/^[ \t]+/, "", str)
  sub(/[ \t]+$/, "", str)
  return str
}

/^Label Time Summary:/ { in_summary = 1; next }
in_summary && /^Total Test time/ {
  gsub(/Total Test time \(real\) = /, "", $0)
  gsub(/ sec/, "", $0)
  gsub(/^[ \t]+/, "", $0)
  print "TOTAL_TEST_TIME=" $0
  in_summary = 0
}
in_summary {
  # Example: "crypto      =   5.40 sec*proc (3 tests)"
  if ($0 ~ /^[[:space:]]*[A-Za-z0-9._-]+[[:space:]]*=/) {
    split($0, parts, "=")
    label = trim_local(parts[1])

    value = parts[2]
    gsub(/^[ \t]+/, "", value)
    time = value
    sub(/ sec\*proc.*/, "", time)
    proc = value
    sub(/.*\(/, "", proc)
    sub(/ tests?\).*/, "", proc)

    print "METRIC_LABEL=" label ", TIME=" time ", PROC=" proc
  }
}

END {
  if (!in_summary) {
    # no-op
  }
}' "$LOG_FILE"

exit 0
