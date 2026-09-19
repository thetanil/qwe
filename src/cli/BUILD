load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "dispatch",
    srcs = ["dispatch.c"],
    hdrs = ["dispatch.h"],
    deps = [
        "//src/cli/encrypt",
        "//src/cli/keygen",
        "//src/cli/run",
        "//src/cli/serve",
        "//src/cli/validate",
        "//src/kernel",
    ],
)

cc_binary(
    name = "qwe",
    srcs = ["main.c"],
    visibility = ["//visibility:public"],
    deps = [":dispatch"],
)

cc_test(
    name = "dispatch_test",
    srcs = ["dispatch_test.c"],
    deps = [
        ":dispatch",
        "//third_party/greatest",
    ],
)
