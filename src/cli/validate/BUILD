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
