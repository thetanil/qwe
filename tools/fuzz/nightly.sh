#!/bin/bash
# The scheduled fuzz run: each target under each sanitizer, one hour apiece in
# parallel, on the persistent corpus in $QWE_FUZZ_DIR (default
# ~/.cache/qwe-fuzz). Exits non-zero if any run produced a crash artifact.
# Meant for CI (a scheduled pipeline with a persistent QWE_FUZZ_DIR); by hand or from cron:
#   17 2 * * *  cd /path/to/qwe && QWE_FUZZ_DIR=/var/lib/qwe-fuzz tools/fuzz/nightly.sh
set -uo pipefail
cd "$(dirname "$0")/../.."
seconds=${1:-3600}
base=${QWE_FUZZ_DIR:-$HOME/.cache/qwe-fuzz}

# One bazel build per sanitizer, then the binaries run side by side (a bazel
# invocation per run would serialise on the output base).
bin=$(mktemp -d)
trap 'rm -rf "$bin"' EXIT
for san in asan ubsan; do
	bazel build --config=fuzz --config=$san //src/edge/yaml:transcode_fuzz //src/edge/yaml:chain_fuzz || exit 1
	for t in transcode chain; do
		cp bazel-bin/src/edge/yaml/${t}_fuzz "$bin/${t}_$san"
	done
done

for san in asan ubsan; do
	for t in transcode chain; do
		c=$base/$t-$san
		mkdir -p "$c" "$c-crashes"
		for f in tests/e2e/*/w.yaml tests/e2e/*/inventory.yaml src/edge/yaml/corpus/*; do
			[ -f "$f" ] && { [ -e "$c/seed-${f//\//_}" ] || cp "$f" "$c/seed-${f//\//_}"; }
		done
		"$bin/${t}_$san" -max_total_time="$seconds" -max_len=65536 -timeout=10 -rss_limit_mb=2048 \
			-dict=src/edge/yaml/qwe.dict -artifact_prefix="$c-crashes/" "$c" >"$c.log" 2>&1 &
	done
done
wait

crashes=$(find "$base" -path '*-crashes/*' -type f | wc -l)
[ "$crashes" -eq 0 ] || { echo "fuzz: $crashes crash artifact(s) under $base/*-crashes/" >&2; exit 1; }
