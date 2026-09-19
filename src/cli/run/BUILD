load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "run",
    srcs = ["run.c"],
    hdrs = ["run.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)
