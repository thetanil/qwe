#!/bin/sh
# usage: summary-link.sh <path>...
#
# Appends a "Source: <link>..." line to $GITHUB_STEP_SUMMARY for each <path> (a file or a
# directory, repo-relative), so a run's summary can be navigated back to the test code that
# produced it. Each link points at this exact commit ($GITHUB_SHA, set by Actions for every
# step) rather than a branch, so it keeps working even if the file is later renamed or
# removed on main -- no line numbers, since those would need updating on every edit to the
# file, but a path is stable.
set -e
: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set (this script only makes sense in a GitHub Actions step)}"
: "${GITHUB_SHA:?GITHUB_SHA is not set}"
: "${GITHUB_STEP_SUMMARY:?GITHUB_STEP_SUMMARY is not set}"

base="https://github.com/$GITHUB_REPOSITORY"
{
	printf 'Source:'
	for p in "$@"; do
		kind=tree
		[ -d "$p" ] || kind=blob
		# shellcheck disable=SC2016 # the backticks are literal markdown, not command substitution
		printf ' [`%s`](%s/%s/%s/%s)' "$p" "$base" "$kind" "$GITHUB_SHA" "$p"
	done
	printf '\n\n'
} >>"$GITHUB_STEP_SUMMARY"
