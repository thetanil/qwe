#!/bin/bash
# usage: tools/clang-tidy/run.sh [--raw] [--evidence-dir DIR]
#
# Default: the pass/fail gate, .clang-tidy as written.
# --raw:           the backlog, not a gate. Every check group .clang-tidy turns on,
#                  with none of its exclusions and none of its CheckOptions, tallied
#                  per check, split into src/+tools/ and *_test.c, and marked with
#                  what hid it from the gate (an exclusion or an option). Always
#                  exits 0 once it has run.
# --evidence-dir:  the gate, and also write version.txt (clang-tidy --version),
#                  .clang-tidy (a copy), files.txt (the exact file list), sha.txt
#                  (the git commit), output.txt (every file's full clang-tidy output,
#                  not --quiet) and exit_status.txt into DIR. CI's evidence artifact
#                  (docs/static-analysis.md, docs/ci-checks.md); a human runs it the
#                  same way to reproduce what an assessor was shown.
#
# tools/clang-tidy/pin.env names the exact clang-tidy CI installs and the finding
# counts in docs/static-analysis.md were measured with. This script only enforces
# the major version (CLANG_TIDY_MAJOR): a devcontainer's apt-installed clang-tidy-20
# tracks Ubuntu's llvm-toolchain-noble-20 packaging, which moves across 20.x point
# releases; CI pins the exact point release itself (.github/actions/clang-tidy-pin).
#
# The LLVM Static Analyzer (clang-analyzer-*, run through clang-tidy) plus a
# popular bugprone/cert/performance/portability ruleset for C -- see
# docs/static-analysis.md. Scope matches the sanitizers (docs/sanitizers.md):
# src/, plugins/ and tools/ in full, third_party/ excluded (vendored, upstream's
# to fix).
#
# No compile_commands.json and no Python (this repo runs neither): `bazel
# aquery` gives the exact per-file compiler invocation Bazel built each source
# with, and this script keeps only the flags clang-tidy's parser needs
# (-iquote/-isystem/-D/-std). The rest (-Wall, -frandom-seed, -MD/-MF, ...) is
# GCC-toolchain plumbing that means nothing to clang and can trip its driver on
# an unrelated GCC-only flag. A file built more than once (rare) is checked
# once, against its first compile action.
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
cd "$here"

usage() {
	echo "usage: tools/clang-tidy/run.sh [--raw] [--evidence-dir DIR]" >&2
	exit 2
}

raw=0
evidence=
while [ $# -gt 0 ]; do
	case "$1" in
	--raw)
		raw=1
		shift
		;;
	--evidence-dir)
		if [ $# -lt 2 ] || [ -z "$2" ]; then
			usage
		fi
		evidence=$2
		shift 2
		;;
	*) usage ;;
	esac
done
[ "$raw" = 1 ] && [ -n "$evidence" ] && usage

: "${CLANG_TIDY:=clang-tidy}"
command -v "$CLANG_TIDY" >/dev/null 2>&1 || {
	echo "run.sh: $CLANG_TIDY not found" >&2
	exit 1
}

# tools/clang-tidy/pin.env: the single file the docs and CI both read.
# shellcheck disable=SC1091
. "$here/tools/clang-tidy/pin.env"
version_line=$("$CLANG_TIDY" --version | grep -oE 'version [0-9]+\.[0-9]+\.[0-9]+' | head -1)
got_major=${version_line#version }
got_major=${got_major%%.*}
if [ "$got_major" != "$CLANG_TIDY_MAJOR" ]; then
	echo "run.sh: $CLANG_TIDY is major version ${got_major:-unknown} (${version_line:-no version output}); tools/clang-tidy/pin.env pins major $CLANG_TIDY_MAJOR (CI runs exactly $CLANG_TIDY_VERSION)" >&2
	exit 1
fi

# Materialize every generated header (LuaJIT's buildvm output, the embedded Lua
# bytecode) that the compile actions below expect to find under bazel-out.
# //src/... and //tools/... name the genrules in src/, but not the ones in
# third_party/ (luajit.h): with a warm disk cache (CI's) and a fresh output
# base, Bazel serves the compile actions from the cache and never writes a
# header nobody asked for, so clang-tidy fails with "'luajit.h' file not found".
# Asking for third_party's genrules by name makes Bazel write their outputs.
mapfile -t generated < <(bazel query 'kind(genrule, //third_party/...)' --output=label 2>/dev/null)
if [ "${#generated[@]}" -eq 0 ]; then
	echo "run.sh: no genrule under //third_party/... (luajit.h has none to build); the generated-header list is empty" >&2
	exit 1
fi
bazel build //src/... //tools/... "${generated[@]}" >&2

query='mnemonic("CppCompile", //src/... + //tools/...)'
aquery_json=$(bazel aquery --output=jsonproto "$query" 2>/dev/null)

mapfile -t files < <(jq -r '
  .actions[].arguments as $a
  | ($a | index("-c")) as $i
  | select($i != null)
  | $a[$i + 1]
' <<<"$aquery_json" | sort -u)

flags_for_file() {
	jq -r --arg f "$1" '
    first(
      .actions[]
      | select(.arguments as $a | ($a | index("-c")) as $i | $i != null and $a[$i + 1] == $f)
    ) as $action
    | $action.arguments as $a
    | [range(0; ($a | length)) as $i
        | if ($a[$i] == "-iquote" or $a[$i] == "-isystem") then [$a[$i], $a[$i + 1]]
          elif ($a[$i] | test("^-D")) then [$a[$i]]
          elif ($a[$i] | test("^-std=")) then [$a[$i]]
          else empty end
      ]
    | flatten | .[]
  ' <<<"$aquery_json"
}

if [ "$raw" = 1 ]; then
	# The groups .clang-tidy enables (its un-negated Checks lines), after a
	# reset: --checks appends to the config's list, so '-*' drops every
	# exclusion and the groups come back whole.
	groups=$(sed -n '/^Checks:/,/^[A-Za-z]/{/^  [a-z]/p}' .clang-tidy | tr -d ' \n')
	# ...and the exclusions, to say which one hid each finding.
	excluded=$(sed -n '/^Checks:/,/^[A-Za-z]/{/^  -[a-z]/p}' .clang-tidy | sed -E 's/^ *-//; s/,$//' | tr '\n' ' ')
	out=$(mktemp)
	# A copy of .clang-tidy without its CheckOptions block, so a check an option
	# narrows (cert-err33-c's CheckedFunctions) runs with its defaults. The
	# options cannot be blanked on the command line: clang-tidy takes
	# --config-file or --config, not both, and --config replaces the file.
	config=$(mktemp)
	trap 'rm -f "$out" "$config"' EXIT
	awk '/^CheckOptions:/ { skip = 1; next } skip && /^[A-Za-z]/ { skip = 0 } !skip' .clang-tidy >"$config"
	for file in "${files[@]}"; do
		mapfile -t flags < <(flags_for_file "$file")
		"$CLANG_TIDY" --quiet --config-file="$config" --checks="-*,${groups%,}" \
			--warnings-as-errors='-*' "$file" -- "${flags[@]}" 2>/dev/null >>"$out" || true
	done
	# A header finding repeats once per file that includes it: count each
	# file:line:col:check once. Paths are made repo-relative first (headers
	# come back absolute). A check .clang-tidy excludes is "excluded"; one it
	# enables can only have been hidden from the gate by a CheckOptions entry.
	grep -E '^[^ ]+:[0-9]+:[0-9]+: (warning|error): .* \[[^]]+\]$' "$out" |
		sed -E "s|^$here/||; s|^\./||" |
		sed -E 's/^([^:]+:[0-9]+:[0-9]+):.*\[([^],]+)[^]]*\]$/\1 \2/' |
		sort -u |
		awk -v excluded="$excluded" '
		BEGIN {
			# .clang-tidy globs (a-b.*) as anchored regexes
			n_ex = split(excluded, ex, " ")
			for (i = 1; i <= n_ex; i++) { gsub(/\./, "\\.", ex[i]); gsub(/\*/, ".*", ex[i]); ex[i] = "^" ex[i] "$" }
		}
		function why(c,   i) { for (i = 1; i <= n_ex; i++) if (c ~ ex[i]) return "excluded"; return "option" }
		{ split($1, loc, ":"); bucket = (loc[1] ~ /_test\.c$/) ? "test" : "src"
		  n[$2, bucket]++; seen[$2] = 1; total[bucket]++; hidden[why($2)]++ }
		END {
			printf "%-60s %9s %8s  %s\n", "check", "src+tools", "*_test.c", "hidden by"
			for (c in seen) printf "%-60s %9d %8d  %s\n", c, n[c, "src"], n[c, "test"], why(c) | "sort"
			close("sort")
			printf "%-60s %9d %8d\n", "total (" total["src"] + total["test"] ")", total["src"], total["test"]
			printf "hidden by an exclusion: %d; by a CheckOptions narrowing: %d\n", hidden["excluded"], hidden["option"]
		}'
	exit 0
fi

if [ -n "$evidence" ]; then
	mkdir -p "$evidence"
	"$CLANG_TIDY" --version >"$evidence/version.txt"
	cp .clang-tidy "$evidence/.clang-tidy"
	printf '%s\n' "${files[@]}" >"$evidence/files.txt"
	git rev-parse HEAD >"$evidence/sha.txt" 2>/dev/null || echo unknown >"$evidence/sha.txt"
	: >"$evidence/output.txt"
fi

fail=0
for file in "${files[@]}"; do
	mapfile -t flags < <(flags_for_file "$file")
	if [ -n "$evidence" ]; then
		# Full output, not --quiet: the evidence bundle keeps the "N warnings
		# generated" summary line too.
		if ! "$CLANG_TIDY" "$file" -- "${flags[@]}" >>"$evidence/output.txt" 2>&1; then
			fail=1
		fi
	elif ! "$CLANG_TIDY" --quiet "$file" -- "${flags[@]}"; then
		fail=1
	fi
done

[ -n "$evidence" ] && echo "$fail" >"$evidence/exit_status.txt"

exit "$fail"
