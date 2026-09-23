load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "encrypt",
    srcs = ["encrypt.c"],
    hdrs = ["encrypt.h"],
    visibility = ["//src/cli:__pkg__"],
    deps = [
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

# Fails every allocation of qwe encrypt in turn (ticket quality/08).
cc_test(
    name = "oom_test",
    size = "small",
    srcs = ["oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        "//src/kernel:fmt",
        ":encrypt",
        "//src/kernel",
        "//src/kernel:oom_shim",
        "//third_party/greatest",
    ],
)
