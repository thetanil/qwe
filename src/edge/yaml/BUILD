load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "yaml",
    srcs = [
        "positions.c",
        "transcode.c",
    ],
    hdrs = [
        "positions.h",
        "secret_tag.h",
        "transcode.h",
    ],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//third_party/libyaml",
        "//third_party/tinycbor",
    ],
)

[cc_test(
    name = name + "_test",
    srcs = [name + "_test.c"],
    deps = [
        ":yaml",
        "//third_party/greatest",
        "//third_party/tinycbor",
    ],
) for name in [
    "limits",
    "magic",
    "positions",
    "tags",
    "transcode",
]]
