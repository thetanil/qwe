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

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)

# Fails the argument list's allocation (ticket quality/08).
cc_test(
    name = "oom_test",
    size = "small",
    srcs = ["oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":run",
        "//src/kernel",
        "//src/kernel:oom_shim",
        "//third_party/greatest",
    ],
)
