#!/bin/bash
# usage: check_version_test.sh <check_version.sh> [case...]
# Cases: match tag_mismatch binary_mismatch prerelease_and_prefix
script=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
shift
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT

hdr() { printf '#define QWE_VERSION "%s"\n' "$1" >"$t/qwe.h"; }
stub() { printf '#!/bin/sh\necho "qwe %s"\n' "$1" >"$t/qwe"; chmod +x "$t/qwe"; }
run() { "$script" "$1" "$t/qwe" "$t/qwe.h" 2>"$t/err"; }

case_match() { hdr 0.1.0; stub 0.1.0; run v0.1.0; }
case_tag_mismatch() {
	hdr 0.1.0; stub 0.1.0
	run v0.2.0 && return 1
	grep -q "v0.2.0" "$t/err" && grep -q "0.1.0" "$t/err"
}
case_binary_mismatch() {
	hdr 0.2.0; stub 0.1.0
	run v0.2.0 && return 1
	grep -q "qwe 0.1.0" "$t/err" && grep -q "0.2.0" "$t/err"
}
case_prerelease_and_prefix() {
	hdr 0.2.0-rc1; stub 0.2.0-rc1
	run v0.2.0-rc1 || return 1
	run 0.2.0-rc1 && return 1
	return 0
}

cases=${*:-match tag_mismatch binary_mismatch prerelease_and_prefix}
rc=0
for c in $cases; do
	if "case_$c"; then echo "PASS: $c"; else echo "FAIL: $c" >&2; rc=1; fi
done
exit $rc
