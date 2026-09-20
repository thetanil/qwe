#!/bin/sh
# usage: bazel run //tools/coverage:html [-- <output dir>]
# Runs `bazel coverage //... --combined_report=lcov` in the workspace, then turns the
# combined report (C and Lua) into an HTML page. Default output: coverage-html/.
# bazel run has released the server by the time this runs, so the nested bazel is fine.
set -e
command -v genhtml >/dev/null || { echo "html: genhtml not found (the lcov package)" >&2; exit 3; }
cd "${BUILD_WORKSPACE_DIRECTORY:?run this with bazel run}"
out=${1:-coverage-html}
bazel coverage //... --combined_report=lcov
genhtml bazel-out/_coverage/_coverage_report.dat --output-directory "$out" >/dev/null
echo "coverage report: $PWD/$out/index.html"
