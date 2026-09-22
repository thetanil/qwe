count_before=$(grep -c '^### w.yaml' summary.md)
[ "$count_before" = 1 ] || { echo "expected one report before the second run, got $count_before" >&2; exit 1; }

"$QWE_BIN" run w.yaml --summary summary.md >/dev/null 2>&1

count_after=$(grep -c '^### w.yaml' summary.md)
[ "$count_after" = 2 ] || { echo "expected two reports after the second run, got $count_after" >&2; exit 1; }
