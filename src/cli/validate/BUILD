load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "validate",
    srcs = ["validate.c"],
    hdrs = ["validate.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)
