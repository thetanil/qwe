load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//src:__subpackages__"])

# Sealing, opening and storing the run key (libsodium).
cc_library(
    name = "secrets",
    srcs = [
        "envelope.c",
        "keyfile.c",
    ],
    hdrs = [
        "envelope.h",
        "keyfile.h",
    ],
    deps = ["//src/kernel:fmt", "//third_party/libsodium"],
)

cc_test(
    name = "envelope_test",
    size = "small",
    srcs = ["envelope_test.c"],
    deps = [
        "//src/kernel:fmt",
        ":secrets",
        "//src/testing:owned",
        "//third_party/greatest",
        "//third_party/libsodium",
    ],
)

cc_test(
    name = "keyfile_test",
    size = "small",
    srcs = ["keyfile_test.c"],
    deps = [
        "//src/kernel:fmt",
        ":secrets",
        "//third_party/greatest",
    ],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)

# Fails every allocation of a seal and of an open in turn (ticket quality/08).
cc_test(
    name = "oom_test",
    size = "small",
    srcs = ["oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        "//src/kernel:fmt",
        ":secrets",
        "//src/kernel:oom_shim",
        "//third_party/greatest",
    ],
)
