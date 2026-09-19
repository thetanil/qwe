load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//src:__subpackages__"])

# Every Lua module and JSON schema that ships inside the binary, as LuaJIT
# bytecode. The key is the name passed to require(); a .json value becomes a
# module returning the file's text. lua-schema's own modules are added below
# from the vendored tree.
MODULES = {
    "backend.local": "//plugins/builtin:backend_local/local.lua",
    "compat53": ":compat53.lua",
    "dkjson": "//third_party/dkjson:dkjson.lua",
    "plugin_schema.file.ensure": "//plugins/builtin:file.ensure/schema.json",
    "qwe.plugins": ":plugins.lua",
    "qwe.validate": ":validate.lua",
    "re": "//third_party/lpeg:re.lua",
    "run": "//plugins/builtin:run/run.lua",
    "workflow_schema": ":workflow.schema.json",
}

# third_party/lua-schema/src/schema/core.lua -> schema.core, and so on.
LUA_SCHEMA = {
    "schema": "src/schema.lua",
    "schema.core": "src/schema/core.lua",
    "schema.draft-07": "src/schema/draft-07.lua",
    "schema.format": "src/schema/format.lua",
    "schema.keyword": "src/schema/keyword.lua",
    "schema.utils": "src/schema/utils.lua",
    "schema.v1-2026": "src/schema/v1-2026.lua",
}

exports_files([
    "validate_test.lua",
    "compat53.lua",
    "plugins.lua",
    "validate.lua",
    "workflow.schema.json",
])

genrule(
    name = "gen_modules",
    srcs = MODULES.values() + ["//third_party/lua-schema:sources"],
    outs = ["modules.c"],
    cmd = "$(location //tools:bcembed) $@ " + " ".join(
        [name + "=$(location " + label + ")" for name, label in MODULES.items()] +
        [name + "=third_party/lua-schema/" + path for name, path in LUA_SCHEMA.items()],
    ),
    tools = ["//tools:bcembed"],
)

cc_library(
    name = "modules",
    srcs = [":gen_modules"],
    hdrs = ["embedded.h"],
)

sh_test(
    name = "validate_test",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location validate_test.lua)",
    ],
    data = [
        "validate_test.lua",
        "//src/kernel:luarun",
    ],
)
