load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "serve",
    srcs = ["serve.c"],
    hdrs = ["serve.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)
