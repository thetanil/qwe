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
    deps = ["//third_party/libsodium"],
)

cc_test(
    name = "envelope_test",
    size = "small",
    srcs = ["envelope_test.c"],
    deps = [
        ":secrets",
        "//third_party/greatest",
        "//third_party/libsodium",
    ],
)

cc_test(
    name = "keyfile_test",
    size = "small",
    srcs = ["keyfile_test.c"],
    deps = [
        ":secrets",
        "//third_party/greatest",
    ],
)
