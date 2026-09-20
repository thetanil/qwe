#!/bin/sh
# usage: alloc_audit_test.sh <audit-file> <source.c>...
#
# Counts the bare allocation calls (malloc, calloc, realloc, strdup, strndup)
# in each non-test source and compares them with the audit. A bare call is
# allowed where its failure is checked at the call site (src/kernel/alloc.h,
# rules 1 and 3); everything else uses the qwe_x* helpers. The audit lists every
# file that has any, with how many, so a new one changes a count and fails here.
# Adding a call means reading it against alloc.h, then updating the audit.
audit=$1
shift
got=$(mktemp) || exit 3
want=$(mktemp) || exit 3
trap 'rm -f "$got" "$want"' EXIT

for f in "$@"; do
	case $f in
	*_test.c | */alloc.c | */oom_shim.c) continue ;;
	esac
	n=$(grep -cE '\b(malloc|calloc|realloc|strdup|strndup)\(' "$f")
	[ "$n" -gt 0 ] && echo "$f $n"
done | sort >"$got"
grep -v '^#' "$audit" | sed '/^$/d' | sort >"$want"

if ! diff -u "$want" "$got" >&2; then
	echo "FAIL: bare allocation calls differ from src/kernel/alloc_audit.txt (- audited, + found)." >&2
	echo "Check each new call against src/kernel/alloc.h, then update the audit." >&2
	exit 1
fi
