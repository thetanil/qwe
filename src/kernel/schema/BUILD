load("@rules_shell//shell:sh_test.bzl", "sh_test")

exports_files(["strict-metaschema.json"], visibility = ["//src:__subpackages__"])

[
    sh_test(
        name = name,
        size = "small",
        srcs = ["//tests:run_lua_test.sh"],
        args = [
            "$(location //src/kernel:luarun)",
            "$(location %s.lua)" % name,
        ],
        data = [
            "%s.lua" % name,
            "//src/kernel:luarun",
        ],
    )
    for name in [
        "strict_meta_test",
        "union_test",
    ]
]
