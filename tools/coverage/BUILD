load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

# lua_instrumented: see defs.bzl.

# One command for the HTML report: bazel run //tools/coverage:html
sh_binary(
    name = "html",
    srcs = ["html.sh"],
)

# Fails if coverage dropped below tools/coverage/floor.txt: bazel run //tools/coverage:check
sh_binary(
    name = "check",
    srcs = ["check.sh"],
)

# The shields.io endpoint document for the coverage percentage.
exports_files(["badge.sh"])

sh_test(
    name = "badge_test",
    size = "small",
    srcs = ["badge_test.sh"],
    args = ["$(location :badge.sh)"],
    data = [":badge.sh"],
)

# Drops third_party/ from the report, for the HTML and the CI totals.
exports_files(["ours.sh"])

sh_test(
    name = "ours_test",
    size = "small",
    srcs = ["ours_test.sh"],
    args = ["$(location :ours.sh)"],
    data = [":ours.sh"],
)
