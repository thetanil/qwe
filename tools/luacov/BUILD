load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

# lua_instrumented: see defs.bzl.

# One command for the HTML report: bazel run //tools/luacov:html
sh_binary(
    name = "html",
    srcs = ["html.sh"],
)
