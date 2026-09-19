# The same workflow against a second inventory gives that inventory's value.
got=$("$QWE_BIN" run w.yaml -i arm.yaml) || exit 1
[ "$got" = "[j] arm" ] || { echo "with arm.yaml: got '$got'" >&2; exit 1; }
