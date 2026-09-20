load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "validate",
    srcs = ["validate.c"],
    hdrs = ["validate.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = ["//src/kernel"],
)

cc_test(
    name = "validate_test",
    size = "small",
    srcs = ["validate_test.c"],
    deps = [
        ":validate",
        "//src/kernel",
        "//third_party/greatest",
    ],
)

# Fails every allocation of a qwe validate run in turn (ticket quality/07).
cc_test(
    name = "oom_test",
    size = "small",
    srcs = ["oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":validate",
        "//src/kernel",
        "//src/kernel:oom_shim",
        "//third_party/greatest",
    ],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)
