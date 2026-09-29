#!/bin/bash
# usage: tools/gcc-analyzer/run.sh [--exact] [--evidence-dir DIR]
#
# The GCC static analyzer gate (ticket sca-round3/07): builds //src/... and //tools/...
# with --config=analyzer (.bazelrc: -fanalyzer on src/ and tools/, third_party/ left
# alone), and fails on any -Wanalyzer-* finding, which -Werror makes a build error.
# It is a second analyzer, independent of clang-tidy's (tools/clang-tidy/run.sh), and
# the two find different things.
#
# Before building it checks two things, and fails if either is off:
#  - the compiler is the GCC tools/gcc-analyzer/pin.env pins: its major version, or
#    with --exact (CI) the exact Ubuntu package version;
#  - every .c under src/, tools/ and plugins/ is analyzed, except the four named in
#    EXCLUDED below.
#
# --evidence-dir:  also write version.txt (gcc --version, the package version and the
#                  cc1 digest), pin.env and bazelrc.txt (the config, copies), sha.txt
#                  (the git commit) and exit_status.txt into DIR on every exit, and
#                  whatever the run got to: preflight.txt (why a check before the build
#                  refused), files.txt (every analyzed source), excluded.txt and
#                  output.txt (the build's full output). CI's evidence artifact, next to
#                  clang-tidy's (docs/static-analysis.md). A file that cannot be written
#                  fails the run (exit 2): evidence that silently lacks a file is worse
#                  than a red job.
#
# The disk cache is keyed by the compiler too. Bazel's action key covers the command
# line and the inputs, and the compiler binary is neither, so after a GCC upgrade (a
# re-pin) an unchanged source would be served from an object the old GCC analyzed and
# passed. run.sh adds --copt=-DQWE_ANALYZER_CC1=<sha256 of cc1, where -fanalyzer
# lives> to the command line, so a different compiler is a different action.
set -u

here=$(cd "$(dirname "$0")/../.." && pwd)
cd "$here" || exit 2

usage() {
	echo "usage: tools/gcc-analyzer/run.sh [--exact] [--evidence-dir DIR]" >&2
	exit 2
}

exact=0
evidence=
while [ $# -gt 0 ]; do
	case $1 in
	--exact) exact=1 ;;
	--evidence-dir)
		[ $# -ge 2 ] || usage
		evidence=$2
		shift
		;;
	*) usage ;;
	esac
	shift
done

# Not compiled by the default configuration, so not analyzed, each for its reason:
#  - the two smoke tests exist to commit a fault (a sanitizer's or valgrind's to
#    catch), and build only under --config=asan/ubsan or --config=valgrind;
#  - the two fuzz harnesses need clang's libFuzzer (--config=fuzz), not GCC.
# clang-tidy lints all four in their own configurations (tools/clang-tidy/run.sh).
EXCLUDED="src/edge/yaml/chain_fuzz.c
src/edge/yaml/transcode_fuzz.c
src/kernel/sanitizer_smoke_test.c
src/kernel/valgrind_smoke_test.c"

tmp=$(mktemp -d) || exit 2

# evidence_write: every step, or exit 2 naming the directory.
evidence_write() {
	"$@" || {
		echo "run.sh: cannot write the evidence to $evidence" >&2
		exit 2
	}
}

# On every exit: what the run got to, then its exit status, into the evidence.
finish() {
	status=$?
	if [ -n "$evidence" ] && [ -d "$evidence" ]; then
		for f in preflight.txt files.txt excluded.txt output.txt; do
			if [ -f "$tmp/$f" ] && ! cp "$tmp/$f" "$evidence/$f"; then
				echo "run.sh: cannot write the evidence to $evidence" >&2
				status=2
			fi
		done
		if ! echo "$status" >"$evidence/exit_status.txt"; then
			echo "run.sh: cannot write the evidence to $evidence" >&2
			status=2
		fi
	fi
	rm -rf "$tmp"
	exit "$status"
}
trap finish EXIT

# refuse CODE MESSAGE...: a check before the build failed; the reason goes to stderr and
# to the evidence.
refuse() {
	code=$1
	shift
	printf '%s\n' "$@" | tee -a "$tmp/preflight.txt" >&2
	exit "$code"
}

. "$here/tools/gcc-analyzer/pin.env"
pkg_version=$(dpkg-query -W -f='${Version}' "$GCC_PACKAGE" 2>/dev/null)
cc1=$(gcc -print-prog-name=cc1)
cc1_sha=$(sha256sum "$cc1" 2>/dev/null | cut -c1-16)

if [ -n "$evidence" ]; then
	evidence_write mkdir -p "$evidence"
	evidence_write cp tools/gcc-analyzer/pin.env "$evidence/pin.env"
	evidence_write eval 'grep -E "^build:analyzer " .bazelrc >"$evidence/bazelrc.txt"'
	evidence_write eval 'git rev-parse HEAD >"$evidence/sha.txt" 2>/dev/null || echo unknown >"$evidence/sha.txt"'
	evidence_write eval '{ gcc --version; echo "$GCC_PACKAGE ${pkg_version:-not installed}"; echo "cc1 $cc1 sha256 ${cc1_sha:-unknown}"; } >"$evidence/version.txt"'
fi

gcc_major=$(gcc -dumpversion | cut -d. -f1)
[ "$gcc_major" = "$GCC_MAJOR" ] ||
	refuse 1 "run.sh: gcc is major version ${gcc_major:-unknown}; tools/gcc-analyzer/pin.env pins $GCC_MAJOR (CI runs exactly $GCC_PACKAGE $GCC_PACKAGE_VERSION)"
if [ "$exact" = 1 ] && [ "$pkg_version" != "$GCC_PACKAGE_VERSION" ]; then
	refuse 1 "run.sh: $GCC_PACKAGE is ${pkg_version:-not installed}; tools/gcc-analyzer/pin.env pins $GCC_PACKAGE_VERSION." \
		"run.sh: the analyzer's findings change between GCC builds: re-run, triage, then re-pin (see pin.env)."
fi
[ -n "$cc1_sha" ] || refuse 2 "run.sh: cannot read gcc's cc1 ($cc1) to key the cache by it"
key=--copt=-DQWE_ANALYZER_CC1=$cc1_sha

# The sources the analyzer config really compiles with -fanalyzer, from bazel's own
# command lines (no build needed), checked against the .c files on disk.
bazel aquery --config=analyzer "$key" 'mnemonic("CppCompile", //src/... + //tools/...)' --output=jsonproto 2>"$tmp/aquery.err" |
	jq -r '.actions[]? | select(.arguments != null) | select(any(.arguments[]; . == "-fanalyzer"))
		| .arguments as $a | $a[($a | index("-c")) + 1]' | sort -u >"$tmp/files.txt"
if [ ! -s "$tmp/files.txt" ]; then
	refuse 2 "$(cat "$tmp/aquery.err")" "run.sh: bazel aquery found no source compiled with -fanalyzer"
fi
echo "$EXCLUDED" | sort >"$tmp/excluded.txt"
git ls-files 'src/*.c' 'tools/*.c' 'plugins/*.c' | sort | comm -23 - "$tmp/files.txt" >"$tmp/missing.txt"
if ! cmp -s "$tmp/missing.txt" "$tmp/excluded.txt"; then
	refuse 1 "run.sh: the analyzed sources and the .c files on disk disagree beyond EXCLUDED:" \
		"$(diff "$tmp/excluded.txt" "$tmp/missing.txt" | grep '^[<>]' | sed -e 's/^>/  not analyzed:/' -e 's/^</  excluded, but analyzed or gone:/')"
fi
echo "run.sh: gcc $(gcc -dumpfullversion) (${pkg_version:-no package}, cc1 $cc1_sha), $(wc -l <"$tmp/files.txt") sources, $(wc -l <"$tmp/excluded.txt") excluded"

bazel build --config=analyzer "$key" --keep_going //src/... //tools/... >"$tmp/output.txt" 2>&1
status=$?
grep -E '(src|tools|plugins)/[^:]+:[0-9]+:[0-9]+: (warning|error): .*\[-W(error=)?analyzer-' "$tmp/output.txt" >"$tmp/findings.txt"
findings=$(wc -l <"$tmp/findings.txt")
cat "$tmp/findings.txt"

if [ "$status" -ne 0 ]; then
	tail -40 "$tmp/output.txt" >&2
	echo "run.sh: $findings -Wanalyzer finding(s) in src/ or tools/; the build failed (exit $status)" >&2
	exit 1
fi
echo "run.sh: no -Wanalyzer findings"
