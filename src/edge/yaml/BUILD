load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "yaml",
    srcs = ["transcode.c"],
    hdrs = ["transcode.h"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//third_party/libyaml",
        "//third_party/tinycbor",
    ],
)

cc_test(
    name = "transcode_test",
    srcs = ["transcode_test.c"],
    deps = [
        ":yaml",
        "//third_party/greatest",
        "//third_party/tinycbor",
    ],
)
