load("@rules_cc//cc:defs.bzl", "cc_library")

# Helpers shared by the *_test.c files, never linked into qwe.
cc_library(
    name = "owned",
    testonly = True,
    hdrs = ["owned.h"],
    visibility = ["//src:__subpackages__"],
)

cc_library(
    name = "env",
    testonly = True,
    hdrs = ["env.h"],
    visibility = ["//src:__subpackages__"],
)
