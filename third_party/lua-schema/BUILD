load("@rules_shell//shell:sh_test.bzl", "sh_test")

# lua-schema (see VERSION), MIT. Pure Lua; embedded as bytecode by //src/kernel/lua.
filegroup(
    name = "sources",
    srcs = glob(["src/**/*.lua"]),
    visibility = ["//visibility:public"],
)

# The upstream draft-07 suite, minus the keywords in the script's excluded set.
sh_test(
    name = "json_schema_test_suite",
    size = "small",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location json_schema_test_suite.lua)",
        "$(locations //third_party/json-schema-test-suite:draft7)",
    ],
    data = [
        "json_schema_test_suite.lua",
        "//src/kernel:luarun",
        "//third_party/json-schema-test-suite:draft7",
    ],
)
