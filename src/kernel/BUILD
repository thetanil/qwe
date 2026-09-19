load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//src:__subpackages__"])

# The kernel has no main. Only src/cli/main.c does.
cc_library(
    name = "kernel",
    srcs = [
        "kernel.c",
        "validate.c",
        "workflow.c",
    ],
    hdrs = [
        "qwe.h",
        "validate.h",
    ],
    deps = [
        ":dag",
        ":job_state",
        ":luacbor",
        ":luavm",
        ":proc",
        ":timer",
        ":result",
        ":ring",
        ":sched",
        ":sink",
        "//src/edge/yaml",
        "//third_party/luajit",
    ],
    visibility = ["//src:__subpackages__"],
)

cc_test(
    name = "api_test",
    srcs = ["api_test.c"],
    deps = [
        ":kernel",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "ring",
    srcs = ["ring.c"],
    hdrs = ["ring.h"],
)

cc_test(
    name = "ring_test",
    srcs = ["ring_test.c"],
    deps = [
        ":ring",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "proc",
    srcs = ["proc.c"],
    hdrs = ["proc.h"],
)

# Signals and process groups: its own test binary (spec, "Test strategy").
cc_test(
    name = "proc_test",
    srcs = ["proc_test.c"],
    deps = [
        ":proc",
        ":timer",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "sink",
    srcs = ["sink.c"],
    hdrs = ["sink.h"],
)

cc_library(
    name = "result",
    srcs = ["result.c"],
    hdrs = ["result.h"],
)

cc_library(
    name = "luacbor",
    srcs = ["luacbor.c"],
    hdrs = ["luacbor.h"],
    deps = [
        "//src/edge/yaml",
        "//third_party/luajit",
        "//third_party/tinycbor",
    ],
)

cc_library(
    name = "luavm",
    srcs = ["luavm.c"],
    hdrs = ["luavm.h"],
    deps = [
        ":luacbor",
        "//src/kernel/lua:modules",
        "//third_party/lpeg",
        "//third_party/luajit",
    ],
)

cc_library(
    name = "dag",
    srcs = ["dag.c"],
    hdrs = ["dag.h"],
)

cc_test(
    name = "dag_test",
    srcs = ["dag_test.c"],
    deps = [
        ":dag",
        ":job_state",
        "//third_party/greatest",
    ],
)

# Runs a Lua script inside qwe's Lua state. Used by tests.
cc_binary(
    name = "luarun",
    srcs = ["luarun.c"],
    visibility = ["//visibility:public"],
    deps = [
        ":luavm",
        "//third_party/luajit",
    ],
)

cc_test(
    name = "lua_cbor_test",
    srcs = ["lua_cbor_test.c"],
    deps = [
        ":luacbor",
        ":luavm",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)

sh_test(
    name = "luarun_test",
    srcs = ["luarun_test.sh"],
    args = ["$(location :luarun)"],
    data = [":luarun"],
)

cc_library(
    name = "job_state",
    srcs = ["job_state.c"],
    hdrs = ["job_state.h"],
)

# Forks to check that an illegal transition aborts.
cc_test(
    name = "job_state_test",
    srcs = ["job_state_test.c"],
    deps = [
        ":job_state",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "timer",
    srcs = ["timer.c"],
    hdrs = ["timer.h"],
)

# Uses real timerfds and sleeps briefly, so it is its own test binary.
cc_test(
    name = "timer_test",
    srcs = ["timer_test.c"],
    deps = [
        ":timer",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "sched",
    srcs = ["sched.c"],
    hdrs = ["sched.h"],
    deps = [":job_state"],
)

cc_test(
    name = "sched_test",
    srcs = ["sched_test.c"],
    deps = [
        ":job_state",
        ":sched",
        "//third_party/greatest",
    ],
)
