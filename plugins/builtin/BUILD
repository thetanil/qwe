load("//tools/coverage:defs.bzl", "lua_instrumented")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

# Sources of the built-in plugins. They are compiled into the binary by
# //src/kernel/lua:modules.
exports_files(glob([
    "**/*.lua",
    "**/*.json",
]))

filegroup(
    name = "sources",
    srcs = glob([
        "**/*.lua",
        "**/*.json",
    ]),
)

# Every built-in plugin passes the metaschema, contract and luacheck checks.
sh_test(
    name = "lint_test",
    size = "small",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location lint_test.lua)",
        "$(locations :plugin_lua)",
    ],
    data = [
        "lint_test.lua",
        ":plugin_lua",
        ":sources",
        "//src/kernel:luarun",
    ],
)

filegroup(
    name = "plugin_lua",
    srcs = glob(["*/*.lua"], exclude = ["*/test.lua"]),
)

# Runs plugins/builtin/<name>/test.lua in qwe's Lua state.
[
    sh_test(
        name = name + "_plugin_test",
        size = "small",
        srcs = ["//tests:run_lua_test.sh"],
        args = [
            "$(location //src/kernel:luarun)",
            "$(location run_plugin_test.lua)",
            "$(location " + name + "/test.lua)",
        ],
        data = [
            "run_plugin_test.lua",
            name + "/test.lua",
            "//src/kernel:luarun",
        ],
    )
    for name in [
        "file.ensure",
        "file.read",
        "file.line",
        "apt.package",
        "assert",
        "backend-ssh",
        "recording",
    ]
]

# The Lua that ships, for `bazel coverage` (see //tools/coverage:defs.bzl).
lua_instrumented(
    name = "shipped",
    srcs = [":plugin_lua"],
    visibility = ["//visibility:public"],
)
