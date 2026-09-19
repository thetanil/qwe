load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//src:__subpackages__"])

# The kernel has no main. Only src/cli/main.c does.
cc_library(
    name = "kernel",
    srcs = [
        "kernel.c",
        "workflow.c",
    ],
    hdrs = ["qwe.h"],
    deps = [
        ":luabridge",
        ":proc",
        ":result",
        ":ring",
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
    name = "luabridge",
    srcs = ["luabridge.c"],
    hdrs = ["luabridge.h"],
    deps = [
        "//plugins/builtin",
        "//third_party/luajit",
        "//third_party/tinycbor",
    ],
)
