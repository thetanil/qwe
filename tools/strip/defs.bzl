"""stripped_binary: a copy of an executable with its symbols removed."""

load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cpp_toolchain", "use_cc_toolchain")

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    src = ctx.file.src
    if ctx.attr.keep_symbols:
        # Sanitizer and valgrind builds need the symbols to report anything readable.
        ctx.actions.run_shell(inputs = [src], outputs = [out], command = "cp -L \"$1\" \"$2\"", arguments = [src.path, out.path])
    else:
        cc = find_cpp_toolchain(ctx)
        ctx.actions.run(
            inputs = depset([src], transitive = [cc.all_files]),
            outputs = [out],
            executable = cc.strip_executable,
            arguments = ["-o", out.path, src.path],
            mnemonic = "StripBinary",
        )
    return [DefaultInfo(files = depset([out]), executable = out)]

stripped_binary = rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True, executable = True, cfg = "target"),
        "keep_symbols": attr.bool(default = False),
    },
    executable = True,
    toolchains = use_cc_toolchain(),
    fragments = ["cpp"],
)
