#!/bin/sh
# usage: gen_secrets.sh <qwe-binary> <smoke-dir>
#
# Generates and runs smoke_secrets.yml from smoke_secrets.yml.in: a fresh HOME, a fresh
# `qwe keygen`, and a random per-run value (so a leak cannot hide behind a fixed string)
# encrypted through `qwe encrypt`, substituted for ENVELOPE in the template. The generated
# file lives only in this script's own scratch work dir and is never committed -- the blob
# is key-specific.
#
# After the run, asserts the value appears nowhere in plaintext: not in the --summary
# file, not in result.json, not in lifecycle.trace. The job log is asserted to contain the
# redaction marker "***" instead -- proof qwe saw the value and redacted it, not that it
# never appeared at all.
#
# Exit: 0 pass; 1 the workflow itself failed, or a leak was found; 3 harness error.
set -e
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
dir=$(cd "$2" && pwd)
[ -x "$qwe" ] || { echo "gen_secrets: no binary $qwe" >&2; exit 3; }
[ -f "$dir/smoke_secrets.yml.in" ] || { echo "gen_secrets: no $dir/smoke_secrets.yml.in" >&2; exit 3; }

work=$(mktemp -d) || exit 3
trap 'rm -rf "$work"' EXIT
export HOME="$work/home"
mkdir -p "$HOME"

"$qwe" keygen >/dev/null

value=$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')
envelope=$(printf %s "$value" | "$qwe" encrypt)

mkdir -p "$work/run"
sed "s|ENVELOPE|$envelope|" "$dir/smoke_secrets.yml.in" >"$work/run/smoke_secrets.yml"

cd "$work/run"
"$qwe" validate smoke_secrets.yml
"$qwe" run smoke_secrets.yml --summary summary.md

fail=0
grep -qF "$value" summary.md && { echo "gen_secrets: the value leaked into summary.md" >&2; fail=1; }

run_dir=$(find .qwe/runs -mindepth 1 -maxdepth 1 -type d)
[ -n "$run_dir" ] || { echo "gen_secrets: no run directory" >&2; exit 3; }

if grep -qF "$value" "$run_dir/result.json" "$run_dir/lifecycle.trace" 2>/dev/null; then
	echo "gen_secrets: the value leaked into result.json or lifecycle.trace" >&2
	fail=1
fi

log="$run_dir/secrets.log"
[ -f "$log" ] || { echo "gen_secrets: no job log $log" >&2; exit 3; }
grep -qF "$value" "$log" && { echo "gen_secrets: the value leaked into the job log" >&2; fail=1; }
grep -qF '***' "$log" || { echo "gen_secrets: no redaction marker in the job log" >&2; fail=1; }

[ "$fail" = 0 ] && echo "PASS: smoke_secrets.yml (no leak found)"
exit $fail
