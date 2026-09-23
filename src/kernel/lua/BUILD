load("@rules_cc//cc:defs.bzl", "cc_library")
load("//tools/coverage:defs.bzl", "lua_instrumented")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//src:__subpackages__"])

# Every Lua module and JSON schema that ships inside the binary, as LuaJIT
# bytecode. The key is the name passed to require(); a .json value becomes a
# module returning the file's text. lua-schema's and luacheck's own modules are
# added below from the vendored trees.
MODULES = {
    "backend.local": "//plugins/builtin:backend_local/local.lua",
    "backend.ssh": "//plugins/builtin:backend-ssh/ssh.lua",
    "backend.recording": "//plugins/builtin:recording/recording.lua",
    "compat53": ":compat53.lua",
    "dkjson": "//third_party/dkjson:dkjson.lua",
    "inventory_schema": ":inventory.schema.json",
    "plugin.assert": "//plugins/builtin:assert/plugin.lua",
    "plugin.file.ensure": "//plugins/builtin:file.ensure/plugin.lua",
    "plugin.file.read": "//plugins/builtin:file.read/plugin.lua",
    "plugin.run": "//plugins/builtin:run/plugin.lua",
    "plugin_schema.assert": "//plugins/builtin:assert/schema.json",
    "plugin_schema.file.ensure": "//plugins/builtin:file.ensure/schema.json",
    "plugin_schema.file.read": "//plugins/builtin:file.read/schema.json",
    "plugin_schema.run": "//plugins/builtin:run/schema.json",
    "qwe.become": ":become.lua",
    "qwe.checkapply": ":checkapply.lua",
    "qwe.inventory": ":inventory.lua",
    "qwe.luacov": ":luacov.lua",
    "qwe.plugincheck": ":plugincheck.lua",
    "qwe.pluginshape": ":pluginshape.lua",
    "qwe.plugins": ":plugins.lua",
    "qwe.strict": ":strict.lua",
    "qwe.template": ":template.lua",
    "qwe.validate": ":validate.lua",
    "re": "//third_party/lpeg:re.lua",
    "strict_metaschema": "//src/kernel/schema:strict-metaschema.json",
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

# The part of luacheck that its API (luacheck.check_strings) needs. The command
# line (main, runner, config, cache, fs, ...) is not embedded, so neither are
# argparse and lfs.
LUACHECK = {
    "luacheck": "src/luacheck/init.lua",
    "luacheck.builtin_standards": "src/luacheck/builtin_standards/init.lua",
    "luacheck.builtin_standards.love": "src/luacheck/builtin_standards/love.lua",
    "luacheck.builtin_standards.minetest": "src/luacheck/builtin_standards/minetest.lua",
    "luacheck.builtin_standards.ngx": "src/luacheck/builtin_standards/ngx.lua",
    "luacheck.builtin_standards.playdate": "src/luacheck/builtin_standards/playdate.lua",
    "luacheck.stages": "src/luacheck/stages/init.lua",
} | {
    "luacheck." + name: "src/luacheck/" + name + ".lua"
    for name in [
        "check",
        "check_state",
        "core_utils",
        "decoder",
        "filter",
        "format",
        "lexer",
        "options",
        "parser",
        "standards",
        "unicode",
        "unicode_printability_boundaries",
        "utils",
        "version",
    ]
} | {
    "luacheck.stages." + name: "src/luacheck/stages/" + name + ".lua"
    for name in [
        "detect_bad_whitespace",
        "detect_compound_operators",
        "detect_cyclomatic_complexity",
        "detect_empty_blocks",
        "detect_empty_statements",
        "detect_globals",
        "detect_reversed_fornum_loops",
        "detect_unbalanced_assignments",
        "detect_uninit_accesses",
        "detect_unreachable_code",
        "detect_unused_fields",
        "detect_unused_locals",
        "linearize",
        "name_functions",
        "parse",
        "parse_inline_options",
        "resolve_locals",
        "unwrap_parens",
    ]
}

exports_files([
    "become.lua",
    "checkapply.lua",
    "validate_test.lua",
    "compat53.lua",
    "inventory.lua",
    "inventory.schema.json",
    "luacov.lua",
    "luacov_test.lua",
    "plugincheck.lua",
    "pluginshape.lua",
    "plugins.lua",
    "strict.lua",
    "template.lua",
    "pluginshape_test.lua",
    "template_test.lua",
    "validate.lua",
    "workflow.schema.json",
])

genrule(
    name = "gen_modules",
    srcs = MODULES.values() + [
        "//third_party/lua-schema:sources",
        "//third_party/luacheck:sources",
    ],
    outs = ["modules.c"],
    cmd = "$(location //tools:bcembed) $@ " + " ".join(
        [name + "=$(location " + label + ")" for name, label in MODULES.items()] +
        [name + "=third_party/lua-schema/" + path for name, path in LUA_SCHEMA.items()] +
        [name + "=third_party/luacheck/" + path for name, path in LUACHECK.items()],
    ),
    tools = ["//tools:bcembed"],
)

cc_library(
    name = "modules",
    srcs = [":gen_modules"],
    hdrs = ["embedded.h"],
)

sh_test(
    name = "template_test",
    size = "small",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location template_test.lua)",
    ],
    data = [
        "pluginshape_test.lua",
    "template_test.lua",
        "//src/kernel:luarun",
    ],
)

sh_test(
    name = "validate_test",
    size = "small",
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

sh_test(
    name = "pluginshape_test",
    size = "small",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location pluginshape_test.lua)",
    ],
    data = [
        "pluginshape_test.lua",
        "//src/kernel:luarun",
    ],
)

sh_test(
    name = "luacov_test",
    size = "small",
    srcs = ["//tests:run_lua_test.sh"],
    args = [
        "$(location //src/kernel:luarun)",
        "$(location luacov_test.lua)",
    ],
    data = [
        "luacov_test.lua",
        "//src/kernel:luarun",
    ],
)

# The Lua that ships, for `bazel coverage` (see //tools/coverage:defs.bzl).
lua_instrumented(
    name = "shipped",
    # luacov.lua is the measuring tool; its own test replaces its hook, so it cannot measure itself.
    srcs = glob(["*.lua"], exclude = ["*_test.lua", "luacov.lua"]),
    visibility = ["//visibility:public"],
)
