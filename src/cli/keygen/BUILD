load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "keygen",
    srcs = ["keygen.c"],
    hdrs = ["keygen.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = [
        "//src/kernel:put",
        "//src/kernel",
        "//src/secrets",
        "//third_party/libsodium",
    ],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)
