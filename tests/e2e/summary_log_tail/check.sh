# Only the last 20 of the step's 25 lines survive into the log tail.
grep -q "<details>" summary.md || { echo "no details block" >&2; exit 1; }
grep -qx "line 6" summary.md || { echo "line 6 missing from the tail" >&2; exit 1; }
grep -qx "line 25" summary.md || { echo "line 25 missing from the tail" >&2; exit 1; }
! grep -qx "line 5" summary.md || { echo "line 5 should have fallen out of the tail" >&2; exit 1; }
