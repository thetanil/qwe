load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//src:__subpackages__"])

# The kernel has no main. Only src/cli/main.c does.
cc_library(
    name = "kernel",
    srcs = ["kernel.c"],
    hdrs = ["qwe.h"],
    visibility = ["//src:__subpackages__"],
)

cc_test(
    name = "api_test",
    srcs = ["api_test.c"],
    deps = [
        ":kernel",
        "//third_party/greatest",
    ],
)
