#!/bin/bash
# Runs one libFuzzer target against a persistent corpus.
#
#   tools/fuzz/run.sh <transcode|chain> [seconds] [sanitizer: asan|ubsan]
#
# The corpus persists in $QWE_FUZZ_DIR/<target> (default: ~/.cache/qwe-fuzz),
# seeded on every run from the e2e workflows and inventories and from the
# regression corpus. New inputs libFuzzer finds are added there; a crash is
# written to $QWE_FUZZ_DIR/<target>-crashes/. Copy a crash into
# src/edge/yaml/corpus/ once fixed. Run from the workspace root of this repo.
set -euo pipefail

target=${1:?target: transcode or chain}
seconds=${2:-3600}
san=${3:-asan}
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

base=${QWE_FUZZ_DIR:-$HOME/.cache/qwe-fuzz}
corpus=$base/$target
crashes=$base/$target-crashes
mkdir -p "$corpus" "$crashes"

bazel build --config=fuzz --config="$san" "//src/edge/yaml:${target}_fuzz"

for f in tests/e2e/*/w.yaml tests/e2e/*/inventory.yaml src/edge/yaml/corpus/*; do
	[ -f "$f" ] || continue
	[ -e "$corpus/seed-${f////_}" ] || cp "$f" "$corpus/seed-${f////_}"
done

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
	"bazel-bin/src/edge/yaml/${target}_fuzz" \
	-max_total_time="$seconds" -max_len=65536 -timeout=10 -rss_limit_mb=2048 \
	-dict=src/edge/yaml/qwe.dict -artifact_prefix="$crashes/" "$corpus"
