load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

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
