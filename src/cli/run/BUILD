load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "run",
    srcs = ["run.c"],
    hdrs = ["run.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)

cc_test(
    name = "run_test",
    size = "small",
    srcs = ["run_test.c"],
    deps = [
        ":run",
        "//src/kernel",
        "//third_party/greatest",
    ],
)
