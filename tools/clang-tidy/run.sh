#!/bin/bash
# usage: tools/clang-tidy/run.sh [--raw]
#
# Default: the pass/fail gate, .clang-tidy as written.
# --raw:   the backlog, not a gate. Every check group .clang-tidy turns on, with
#          none of its exclusions and none of its CheckOptions, tallied per
#          check, split into src/+tools/ and *_test.c, and marked with what hid
#          it from the gate (an exclusion or an option). Always exits 0 once it
#          has run.
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

raw=0
case "${1-}" in
--raw) raw=1 ;;
"") ;;
*)
	echo "usage: tools/clang-tidy/run.sh [--raw]" >&2
	exit 2
	;;
esac

: "${CLANG_TIDY:=clang-tidy}"
command -v "$CLANG_TIDY" >/dev/null 2>&1 || {
	echo "run.sh: $CLANG_TIDY not found" >&2
	exit 1
}

# Materialize every generated header (LuaJIT's buildvm output, the embedded Lua
# bytecode) that the compile actions below expect to find under bazel-out.
bazel build //src/... //tools/... >&2

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

fail=0
for file in "${files[@]}"; do
	mapfile -t flags < <(flags_for_file "$file")
	if ! "$CLANG_TIDY" --quiet "$file" -- "${flags[@]}"; then
		fail=1
	fi
done

exit "$fail"
