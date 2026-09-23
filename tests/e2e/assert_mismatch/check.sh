# The equals mismatch is covered by qwe's own stdout/exit above. contains and matches
# get the same shape: exit 1 and exactly the message form, run here through $QWE_BIN.
run_and_check() {
  workflow=$1 want=$2
  out=$("$QWE_BIN" run "$workflow" 2>&1); code=$?
  [ "$code" = 1 ] || { echo "$workflow: expected exit 1, got $code" >&2; exit 1; }
  [ "$out" = "[j] $want" ] || { echo "$workflow: message mismatch:" >&2; echo "  want: [j] $want" >&2; echo "  got:  $out" >&2; exit 1; }
}
run_and_check contains.yaml "qwe: plugin failed: assert: values differ: expected contains xyz, actual hello world"
run_and_check matches.yaml "qwe: plugin failed: assert: values differ: expected matches ^build%-%d+\$, actual build-abc"
