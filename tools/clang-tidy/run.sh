#!/bin/bash
# usage: tools/clang-tidy/run.sh [--list] [--raw] [--evidence-dir DIR]
#
# Default: the pass/fail gate, .clang-tidy as written.
# --list:          print the (file, flag set) pairs the gate lints and their count,
#                  then exit. Needs Bazel only (no build, no clang-tidy).
#                  tools/clang-tidy/coverage_test.sh checks it against the .c files on disk.
# --raw:           the backlog, not a gate. Every check group .clang-tidy turns on,
#                  with none of its exclusions and none of its CheckOptions, tallied
#                  per check, split into src/+tools/ and *_test.c, and marked with
#                  what hid it from the gate (an exclusion or an option). Always
#                  exits 0 once it has run.
# --evidence-dir:  the gate, and also write version.txt (clang-tidy --version),
#                  .clang-tidy (a copy), files.txt (the (file, flag set) list and
#                  its count), sha.txt (the git commit), output.txt (every run's full
#                  clang-tidy output, not --quiet), ctu-map.txt (the cross-translation-unit
#                  definition map, see below) and exit_status.txt into DIR. CI's
#                  evidence artifact (docs/static-analysis.md, docs/ci-checks.md); a
#                  human runs it the same way to reproduce what an assessor was shown.
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
# an unrelated GCC-only flag.
#
# What gets linted is every distinct (file, flag set) pair, not every file: the code a
# gate skips is exactly the code built with unusual flags. A pair is one CppCompile
# action's kept flags, with the configuration's directory name in bazel-out/ folded
# away (the exec configuration's hash is not a difference the preprocessor sees).
# Three things widen the set beyond `//src/... + //tools/...` in the default
# configuration:
#   - a file Bazel compiles more than once (trace.c's errno_name_test variant, the
#     exec-configuration copies of alloc.c and errstr.c that carry -DNDEBUG) is linted
#     once per flag set;
#   - `manual`-tagged cc targets (the libFuzzer binaries): Bazel's wildcard expansion
#     skips them, so they are named, and printed as they are added;
#   - every .bazelrc configuration in CONFIGS below that changes what the preprocessor
#     sees: coverage (-DQWE_GCOV), valgrind and the sanitizers (a smoke test keys on
#     them), fuzz (--define=qwe_fuzz=1). `release` is left out: it only adds -g,
#     which is not among the kept flags. A configuration that adds no new pair still
#     runs the aquery and shows up in the configuration column of the pairs it shares.
# A finding is reported once, by file:line, with the configurations it appeared under.
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
cd "$here"

usage() {
	echo "usage: tools/clang-tidy/run.sh [--list] [--raw] [--evidence-dir DIR]" >&2
	exit 2
}

list=0
raw=0
evidence=
while [ $# -gt 0 ]; do
	case "$1" in
	--list)
		list=1
		shift
		;;
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
[ "$list" = 1 ] && { [ "$raw" = 1 ] || [ -n "$evidence" ]; } && usage

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The configurations to enumerate, in the order the configuration column lists them.
# `coverage` is a command's options in .bazelrc (`coverage --copt=-DQWE_GCOV`), not a
# --config, so its flags are read from there: a change to .bazelrc is a change here.
CONFIGS=(default coverage fuzz valgrind ubsan asan)
config_flags() {
	case "$1" in
	default) ;;
	# Only the flags that can change what a CppCompile action looks like
	# (--copt/--host_copt/--per_file_copt/--define): `coverage`'s .bazelrc lines also
	# carry --test_env=QWE_LUA_COVERAGE=1, a test-time flag aquery has no compile
	# action to apply it to and no reason to see.
	coverage) sed -n 's/^coverage //p' .bazelrc | tr ' ' '\n' | grep -E '^--(copt|host_copt|per_file_copt|define)=' ;;
	*) echo "--config=$1" ;;
	esac
}

# bazel_out CMD...: run bazel CMD, its stdout to the caller and its stderr shown only
# if it fails. A failed query must stop the script, not read as an empty list.
bazel_out() {
	if ! bazel "$@" 2>"$tmp/bazel.err"; then
		cat "$tmp/bazel.err" >&2
		echo "run.sh: bazel $1 failed" >&2
		exit 1
	fi
}

scope='//src/... + //tools/... + //plugins/...'
# Command substitution into a variable, not `mapfile < <(...)`: a failed query's
# exit status must reach set -e, and process substitution discards it.
manual=()
manual_labels=$(bazel_out query "attr(tags, manual, kind(\"cc_.* rule\", $scope))" --output=label)
if [ -n "$manual_labels" ]; then
	mapfile -t manual <<<"$manual_labels"
fi
query="mnemonic(\"CppCompile\", $scope"
if [ "${#manual[@]}" -gt 0 ]; then
	echo "run.sh: manual-tagged cc targets added to the wildcard: ${manual[*]}" >&2
	query+=$(printf ' + %s' "${manual[@]}")
fi
query+=')'

# Every CppCompile action of every configuration, one JSON object each: the file, the
# kept flags, and a key (the file and its flag set, the bazel-out configuration directory folded
# away). Then one entry per distinct key: the first action's own flags (paths that
# exist on disk), the configurations that produced it, and the variant, the -D/-std
# flags that tell this entry from the file's other ones.
: >"$tmp/actions.jsonl"
for c in "${CONFIGS[@]}"; do
	mapfile -t cflags < <(config_flags "$c")
	bazel_out aquery --output=jsonproto "${cflags[@]}" "$query" >"$tmp/aquery.json"
	jq -c --arg cfg "$c" '
	  .actions[]?
	  | .arguments as $a
	  | ($a | index("-c")) as $i
	  | select($i != null)
	  | [range(0; ($a | length)) as $j
	      | if ($a[$j] == "-iquote" or $a[$j] == "-isystem") then [$a[$j], $a[$j + 1]]
	        elif ($a[$j] | test("^-D|^-std=")) then [$a[$j]]
	        else empty end
	    ] as $groups
	  | { config: $cfg, file: $a[$i + 1], flags: ($groups | flatten),
	      key: [$a[$i + 1], ($groups | map(join(" ") | gsub("bazel-out/[^/]+/bin"; "bazel-out/CFG/bin")) | unique)] }
	' "$tmp/aquery.json" >>"$tmp/actions.jsonl"
done
order=$(printf '%s\n' "${CONFIGS[@]}" | jq -R . | jq -sc .)
jq -sc --argjson order "$order" '
  def isdef: (test("^-D") and (test("^-D__(DATE|TIME|TIMESTAMP)__=") | not)) or test("^-std=");
  group_by(.key)
  | map({ file: .[0].file, flags: .[0].flags,
          configs: ([.[].config] | unique | sort_by(. as $c | $order | index($c))),
          defs: (.[0].flags | map(select(isdef))) })
  | group_by(.file)
  | map( (reduce (.[1:][] | .defs) as $d (.[0].defs; . - (. - $d))) as $common
         | .[] | . + { variant: ((.defs - $common) | join(" ")) } | del(.defs) )
  | sort_by([.file, .variant])
  | .[]
' "$tmp/actions.jsonl" >"$tmp/entries.jsonl"

if [ ! -s "$tmp/entries.jsonl" ]; then
	echo "run.sh: bazel aquery found no CppCompile action ($query)" >&2
	exit 1
fi

# The list: file, configurations, variant (tab-separated), then the count.
{
	jq -r '[.file, (.configs | join(",")), .variant] | @tsv' "$tmp/entries.jsonl"
	echo "$(wc -l <"$tmp/entries.jsonl") (file, flag set) pairs"
} >"$tmp/list.txt"

if [ "$list" = 1 ]; then
	cat "$tmp/list.txt"
	exit 0
fi

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
# bytecode) that the compile actions above expect to find under bazel-out.
# //src/... and //tools/... name the genrules in src/, but not the ones in
# third_party/ (luajit.h): with a warm disk cache (CI's) and a fresh output
# base, Bazel serves the compile actions from the cache and never writes a
# header nobody asked for, so clang-tidy fails with "'luajit.h' file not found".
# Asking for third_party's genrules by name makes Bazel write their outputs.
generated=()
generated_labels=$(bazel_out query 'kind(genrule, //third_party/...)' --output=label)
if [ -n "$generated_labels" ]; then
	mapfile -t generated <<<"$generated_labels"
fi
if [ "${#generated[@]}" -eq 0 ]; then
	echo "run.sh: no genrule under //third_party/... (luajit.h has none to build); the generated-header list is empty" >&2
	exit 1
fi
bazel build //src/... //tools/... "${generated[@]}" >&2

cat "$tmp/list.txt"

# entry_flags ENTRY: the flags of one entries.jsonl line, into $flags.
entry_flags() {
	mapfile -t flags < <(jq -r '.flags[]' <<<"$1")
}
entry_label() {
	jq -r '"\(.file) [\(.configs | join(","))]" + (if .variant == "" then "" else " \(.variant)" end)' <<<"$1"
}

# Cross-translation-unit analysis (.scratch/sca-round3 ticket 06, docs/static-analysis.md
# "What the analyzer sees"). Without it the analyzer stops at every call into another .c:
# a buffer a callee frees, a NULL it returns, an fd it closes are all invisible. With it,
# a call to a function defined in another file of the scope is inlined like a local one.
# On-demand CTU, no AST dumps and no Python (analyze-build is a Python script): the
# analyzer parses the other file itself, from two files in $ctu:
#   externalDefMap.txt  each external function's USR and the file that defines it, from
#                       clang-extdef-mapping (same LLVM major as clang-tidy). A USR defined
#                       in more than one file (every main, greatest, the __wrap_ OOM shims,
#                       a test's stand-in for a library function) is left out: which
#                       definition a call reaches depends on the binary, so none is linked.
#   invocations.yml     the flags each file is parsed with: one entry per file, its
#                       default-configuration flags (fewest extra -D first), as JSON,
#                       which YAML accepts. Its argv[0] is the real path of the clang-tidy
#                       being run: the imported file's builtin headers (stddef.h, ...) are
#                       found relative to it, and with a bare "clang" they came from
#                       whatever clang was on PATH, or none (CI has only the pinned tree).
# ctu-import-threshold is the number of files, so no file's imports are cut short (the
# default, 24, is close to what workflow.c imports). display-ctu-progress prints one
# "CTU loaded AST file" line per import: the gate fails if no run imported anything,
# because a broken map or a path mismatch turns CTU off without a word.
: "${CLANG_EXTDEF_MAPPING:=clang-extdef-mapping}"
command -v "$CLANG_EXTDEF_MAPPING" >/dev/null 2>&1 || {
	echo "run.sh: $CLANG_EXTDEF_MAPPING not found (cross-translation-unit analysis needs it)" >&2
	exit 1
}
extdef_line=$("$CLANG_EXTDEF_MAPPING" --version | grep -oE 'version [0-9]+\.[0-9]+\.[0-9]+' | head -1)
extdef_major=${extdef_line#version }
extdef_major=${extdef_major%%.*}
if [ "$extdef_major" != "$CLANG_TIDY_MAJOR" ]; then
	echo "run.sh: $CLANG_EXTDEF_MAPPING is major version ${extdef_major:-unknown}; clang-tidy is $CLANG_TIDY_MAJOR" >&2
	exit 1
fi
ctu=$tmp/ctu
mkdir -p "$ctu"
jq -sc 'group_by(.file) | map(sort_by([(.configs | index("default") == null), (.variant | length)]) | .[0]) | .[]' \
	"$tmp/entries.jsonl" >"$ctu/chosen.jsonl"
: >"$ctu/defs.txt"
while IFS= read -r entry; do
	entry_flags "$entry"
	file=$(jq -r .file <<<"$entry")
	if ! "$CLANG_EXTDEF_MAPPING" "$file" -- "${flags[@]}" >>"$ctu/defs.txt" 2>"$ctu/extdef.err"; then
		cat "$ctu/extdef.err" >&2
		echo "run.sh: $CLANG_EXTDEF_MAPPING failed on $file" >&2
		exit 1
	fi
done <"$ctu/chosen.jsonl"
sort -u "$ctu/defs.txt" | awk '{ n[$1]++; line[$1] = $0 } END { for (u in n) if (n[u] == 1) print line[u] }' |
	sort >"$ctu/externalDefMap.txt"
jq -s --arg here "$here" --arg driver "$(readlink -f "$(command -v "$CLANG_TIDY")")" 'map({ key: "\($here)/\(.file)", value: ([$driver] + .flags + ["-c", "\($here)/\(.file)"]) }) | from_entries' \
	"$ctu/chosen.jsonl" >"$ctu/invocations.yml"
ctu_args=(-Xclang -analyzer-config -Xclang
	"experimental-enable-naive-ctu-analysis=true,ctu-dir=$ctu,ctu-invocation-list=$ctu/invocations.yml,ctu-import-threshold=$(wc -l <"$ctu/chosen.jsonl"),display-ctu-progress=true")

if [ "$raw" = 1 ]; then
	# The groups .clang-tidy enables (its un-negated Checks lines), after a
	# reset: --checks appends to the config's list, so '-*' drops every
	# exclusion and the groups come back whole.
	groups=$(sed -n '/^Checks:/,/^[A-Za-z]/{/^  [a-z]/p}' .clang-tidy | tr -d ' \n')
	# ...and the exclusions, to say which one hid each finding.
	excluded=$(sed -n '/^Checks:/,/^[A-Za-z]/{/^  -[a-z]/p}' .clang-tidy | sed -E 's/^ *-//; s/,$//' | tr '\n' ' ')
	out=$tmp/raw.out
	# A copy of .clang-tidy without its CheckOptions block, so a check an option
	# narrows (cert-err33-c's CheckedFunctions) runs with its defaults. The
	# options cannot be blanked on the command line: clang-tidy takes
	# --config-file or --config, not both, and --config replaces the file.
	config=$tmp/raw.clang-tidy
	awk '/^CheckOptions:/ { skip = 1; next } skip && /^[A-Za-z]/ { skip = 0 } !skip' .clang-tidy >"$config"
	while IFS= read -r entry; do
		entry_flags "$entry"
		file=$(jq -r .file <<<"$entry")
		"$CLANG_TIDY" --quiet --config-file="$config" --checks="-*,${groups%,}" \
			--warnings-as-errors='-*' "$file" -- "${flags[@]}" "${ctu_args[@]}" 2>/dev/null >>"$out" || true
	done <"$tmp/entries.jsonl"
	# A header finding repeats once per file that includes it, and a file finding
	# once per flag set: count each file:line:col:check once. Paths are made
	# repo-relative first (headers come back absolute). A check .clang-tidy excludes
	# is "excluded"; one it enables can only have been hidden from the gate by a
	# CheckOptions entry.
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
	cp "$tmp/list.txt" "$evidence/files.txt"
	git rev-parse HEAD >"$evidence/sha.txt" 2>/dev/null || echo unknown >"$evidence/sha.txt"
	: >"$evidence/output.txt"
fi

# Every run's output, each behind a marker naming the pair, for the report below.
# Evidence keeps clang-tidy's full output, "N warnings generated" summary lines too.
fail=0
quiet=--quiet
[ -n "$evidence" ] && quiet=
while IFS= read -r entry; do
	entry_flags "$entry"
	file=$(jq -r .file <<<"$entry")
	printf '@@RUN\t%s\n' "$(entry_label "$entry")" >>"$tmp/all.out"
	# shellcheck disable=SC2086 # $quiet is one flag or nothing
	if ! "$CLANG_TIDY" $quiet "$file" -- "${flags[@]}" "${ctu_args[@]}" >"$tmp/run.out" 2>&1; then
		fail=1
	fi
	cat "$tmp/run.out" >>"$tmp/all.out"
done <"$tmp/entries.jsonl"

# CTU that imported nothing is CTU that is off (see the CTU block above).
ctu_off=0
ctu_loads=$(grep -c '^CTU loaded AST file: ' "$tmp/all.out" || true)
if [ "$ctu_loads" -eq 0 ]; then
	echo "run.sh: cross-translation-unit analysis imported no file in any run: check $CLANG_EXTDEF_MAPPING and the paths in externalDefMap.txt against invocations.yml" >&2
	ctu_off=1
fi
echo "run.sh: cross-translation-unit analysis: $(wc -l <"$ctu/externalDefMap.txt") external definitions, $ctu_loads imports" >&2

[ -n "$evidence" ] && {
	sed "s|$here/||g" "$tmp/all.out" >"$evidence/output.txt"
	sed "s|$here/||g" "$ctu/externalDefMap.txt" >"$evidence/ctu-map.txt"
	echo $((fail | ctu_off)) >"$evidence/exit_status.txt"
}

if [ "$fail" = 1 ]; then
	# One block per finding, however many configurations reported it: keyed on
	# file:line:col and check, printed with the configurations it appeared under.
	report=$(sed "s|$here/||g" "$tmp/all.out" | awk '
		/^@@RUN\t/ { label = substr($0, 7); skip = 0; cur = 0; next }
		/^[^ :]+:[0-9]+:[0-9]+: (warning|error): .* \[[^]]+\]$/ {
			split($0, p, ": "); check = $0; sub(/^.*\[/, "", check); sub(/\]$/, "", check)
			key = p[1] " " check
			if (key in idx) { skip = 1; cur = 0; cfg[idx[key]] = cfg[idx[key]] "; " label; next }
			skip = 0; n++; idx[key] = n; cfg[n] = label; body[n] = $0; cur = n; next
		}
		/^[0-9]+ warnings? generated\.$|^Suppressed [0-9]+ warnings|^Use -header-filter|^Use -system-headers|^Error while processing|^CTU loaded AST file: / { next }
		{ if (!skip && cur) body[cur] = body[cur] "\n" $0 }
		END {
			for (i = 1; i <= n; i++) { print body[i]; print "  in: " cfg[i] }
			print n " finding(s)"
		}')
	if [ "${report##*$'\n'}" = "0 finding(s)" ]; then
		# clang-tidy failed without a diagnostic we can parse (a crash, a bad flag):
		# say so with everything it printed.
		echo "run.sh: clang-tidy exited non-zero without a finding; its output:" >&2
		sed "s|$here/||g" "$tmp/all.out" >&2
	else
		printf '%s\n' "$report" >&2
	fi
fi

exit $((fail | ctu_off))
