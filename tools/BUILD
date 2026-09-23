load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

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

sh_test(
    name = "bcembed_test",
    size = "small",
    srcs = ["bcembed_test.sh"],
    args = ["$(location :bcembed)"],
    data = [":bcembed"],
)
