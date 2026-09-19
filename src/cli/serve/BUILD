load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "serve",
    srcs = ["serve.c"],
    hdrs = ["serve.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)
