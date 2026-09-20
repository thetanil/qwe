"""lua_instrumented: declares Lua sources to `bazel coverage`."""

def _impl(ctx):
    return [
        DefaultInfo(files = depset(ctx.files.srcs)),
        coverage_common.instrumented_files_info(ctx, source_attributes = ["srcs"], extensions = ["lua"]),
    ]

lua_instrumented = rule(
    implementation = _impl,
    attrs = {"srcs": attr.label_list(allow_files = [".lua"])},
    doc = "Puts srcs in the coverage manifest of whatever depends on it (through data). Bazel's lcov merger " +
          "drops a file that is not in that manifest, and the Lua coverage qwe writes would be lost.",
)
