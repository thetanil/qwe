load("@rules_cc//cc:defs.bzl", "cc_library")

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
