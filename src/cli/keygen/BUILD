load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "keygen",
    srcs = ["keygen.c"],
    hdrs = ["keygen.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)
