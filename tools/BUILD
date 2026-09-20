load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "bcembed",
    srcs = ["bcembed.c"],
    copts = ["-std=gnu99"],
    visibility = ["//visibility:public"],
    deps = ["//third_party/luajit"],
)

# The sources, for //src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = ["bcembed.c"],
    visibility = ["//src/kernel:__pkg__"],
)
