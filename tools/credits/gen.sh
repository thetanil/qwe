#!/bin/sh
# usage: bazel run //tools/credits:gen [-- --update]
#
# A markdown table of every vendored third_party/ dependency -- name, upstream,
# version, license -- for the README's Credits section.
#
#   (no argument)  prints the table to stdout
#   --update       rewrites it in place in README.md, between the
#                  <!-- credits:start --> / <!-- credits:end --> markers
#
# The package list is every directory directly under third_party/ that bazel
# recognises as a package (has a BUILD file) -- `bazel query '//third_party/...'`
# alone under-counts: a package whose BUILD is only `exports_files(...)`, like
# dkjson's, defines no target the `...`/`:all` wildcards match, so it never shows up
# in that query. Listing the directories and confirming each has a BUILD file next
# to it is the complete, dialect-independent way to get all of them, dependency of
# the shipped binary or not (a test-only one, like greatest or
# json-schema-test-suite, belongs in the credits too -- it is still vendored code).
#
# Each package's name, version, upstream reference and license come from its own
# third_party/<pkg>/VERSION file -- one line, in the format every vendored package
# already uses in its BUILD file's header comment:
#   <Name> <version> (<upstream ref>)[, <extra>], <License>[. <notes>]
# <upstream ref> is "owner/repo" (assumed GitHub) or a bare domain. Add a VERSION
# file (copy the wording already in the package's BUILD comment) for any package
# this script reports missing one; the check is deliberately loud rather than
# silently skipping a dependency's credit.
set -eu
root=${BUILD_WORKSPACE_DIRECTORY:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$root"

pkgs=$(for d in third_party/*/; do [ -f "$d/BUILD" ] && basename "$d"; done | sort)

rows=$(
	for pkg in $pkgs; do
		f="third_party/$pkg/VERSION"
		[ -f "$f" ] || { echo "credits: $pkg has no $f" >&2; exit 3; }
		head -1 "$f"
	done | awk -F'\t' '
	{
		l = $0
		n = split(l, w, " ")
		name = w[1]
		popen = index(l, "(")
		if (popen == 0) { print "credits: no ( ) in: " l > "/dev/stderr"; exit 1 }
		version = substr(l, length(name) + 1, popen - length(name) - 1)
		gsub(/^[ \t]+|[ \t]+$/, "", version)

		rest = substr(l, popen + 1)
		pclose = index(rest, ")")
		if (pclose == 0) { print "credits: unbalanced ( in: " l > "/dev/stderr"; exit 1 }
		ref = substr(rest, 1, pclose - 1)

		after = substr(rest, pclose + 1)
		sub(/^,[ \t]*/, "", after)
		lic = after
		i = index(lic, ". ")
		j = index(lic, " -- ")
		if (i > 0 && (j == 0 || i < j)) lic = substr(lic, 1, i - 1)
		else if (j > 0) lic = substr(lic, 1, j - 1)
		sub(/\.$/, "", lic)

		slug = ref
		c = index(slug, ",")
		if (c > 0) slug = substr(slug, 1, c - 1)
		gsub(/^[ \t]+|[ \t]+$/, "", slug)
		url = (index(slug, "/") > 0) ? "https://github.com/" slug : "https://" slug

		printf "| %s | [%s](%s) | %s | %s |\n", name, slug, url, version, lic
	}'
)

table=$(printf '| Name | Upstream | Version | License |\n|---|---|---|---|\n%s' "$rows")

if [ "${1:-}" = "--update" ]; then
	awk -v t="$table" '
		/<!-- credits:start -->/ { print; print t; skip = 1; next }
		/<!-- credits:end -->/ { skip = 0 }
		!skip
	' README.md >README.md.new
	grep -q '<!-- credits:start -->' README.md || { echo "credits: README.md has no <!-- credits:start --> marker" >&2; rm -f README.md.new; exit 3; }
	mv README.md.new README.md
	echo "credits: README.md updated ($(echo "$pkgs" | wc -l) dependencies)"
else
	printf '%s\n' "$table"
fi
