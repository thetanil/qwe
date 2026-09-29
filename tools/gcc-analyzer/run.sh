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
# --evidence-dir:  also write version.txt (gcc --version and the package version),
#                  pin.env and bazelrc.txt (the config, copies), files.txt (every
#                  analyzed source), excluded.txt, sha.txt (the git commit),
#                  output.txt (the build's full output) and exit_status.txt into DIR:
#                  CI's evidence artifact, next to clang-tidy's (docs/static-analysis.md).
#
# A cached compile is not re-analyzed, and that is sound: -Werror means a compile with
# a finding fails, and a failed action is never cached.
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

. "$here/tools/gcc-analyzer/pin.env"

gcc_major=$(gcc -dumpversion | cut -d. -f1)
if [ "$gcc_major" != "$GCC_MAJOR" ]; then
	echo "run.sh: gcc is major version ${gcc_major:-unknown}; tools/gcc-analyzer/pin.env pins $GCC_MAJOR (CI runs exactly $GCC_PACKAGE $GCC_PACKAGE_VERSION)" >&2
	exit 1
fi
pkg_version=$(dpkg-query -W -f='${Version}' "$GCC_PACKAGE" 2>/dev/null)
if [ "$exact" = 1 ] && [ "$pkg_version" != "$GCC_PACKAGE_VERSION" ]; then
	echo "run.sh: $GCC_PACKAGE is ${pkg_version:-not installed}; tools/gcc-analyzer/pin.env pins $GCC_PACKAGE_VERSION." >&2
	echo "run.sh: the analyzer's findings change between GCC builds: re-run, triage, then re-pin (see pin.env)." >&2
	exit 1
fi

tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT

# The sources the analyzer config really compiles with -fanalyzer, from bazel's own
# command lines (no build needed), checked against the .c files on disk.
bazel aquery --config=analyzer 'mnemonic("CppCompile", //src/... + //tools/...)' --output=jsonproto 2>"$tmp/aquery.err" |
	jq -r '.actions[]? | select(.arguments != null) | select(any(.arguments[]; . == "-fanalyzer"))
		| .arguments as $a | $a[($a | index("-c")) + 1]' | sort -u >"$tmp/files.txt"
if [ ! -s "$tmp/files.txt" ]; then
	cat "$tmp/aquery.err" >&2
	echo "run.sh: bazel aquery found no source compiled with -fanalyzer" >&2
	exit 2
fi
echo "$EXCLUDED" | sort >"$tmp/excluded.txt"
git ls-files 'src/*.c' 'tools/*.c' 'plugins/*.c' | sort | comm -23 - "$tmp/files.txt" >"$tmp/missing.txt"
if ! cmp -s "$tmp/missing.txt" "$tmp/excluded.txt"; then
	echo "run.sh: the analyzed sources and the .c files on disk disagree beyond EXCLUDED:" >&2
	diff "$tmp/excluded.txt" "$tmp/missing.txt" | grep '^[<>]' | sed -e 's/^>/  not analyzed:/' -e 's/^</  excluded, but analyzed or gone:/' >&2
	exit 1
fi
echo "run.sh: gcc $(gcc -dumpfullversion) (${pkg_version:-no package}), $(wc -l <"$tmp/files.txt") sources, $(wc -l <"$tmp/excluded.txt") excluded"

bazel build --config=analyzer --keep_going //src/... //tools/... >"$tmp/output.txt" 2>&1
status=$?
grep -E '(src|tools|plugins)/[^:]+:[0-9]+:[0-9]+: (warning|error): .*\[-W(error=)?analyzer-' "$tmp/output.txt" >"$tmp/findings.txt"
findings=$(wc -l <"$tmp/findings.txt")
cat "$tmp/findings.txt"

if [ -n "$evidence" ]; then
	mkdir -p "$evidence"
	{
		gcc --version
		echo "$GCC_PACKAGE $pkg_version"
	} >"$evidence/version.txt"
	cp tools/gcc-analyzer/pin.env "$evidence/pin.env"
	grep -E '^build:analyzer ' .bazelrc >"$evidence/bazelrc.txt"
	cp "$tmp/files.txt" "$evidence/files.txt"
	cp "$tmp/excluded.txt" "$evidence/excluded.txt"
	git rev-parse HEAD >"$evidence/sha.txt" 2>/dev/null || echo unknown >"$evidence/sha.txt"
	cp "$tmp/output.txt" "$evidence/output.txt"
	echo "$status" >"$evidence/exit_status.txt"
fi

if [ "$status" -ne 0 ]; then
	tail -40 "$tmp/output.txt" >&2
	echo "run.sh: $findings -Wanalyzer finding(s) in src/ or tools/; the build failed (exit $status)" >&2
	exit 1
fi
echo "run.sh: no -Wanalyzer findings"
