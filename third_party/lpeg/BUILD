load("@rules_cc//cc:defs.bzl", "cc_library")

# LPeg 1.1.0, MIT. C module; luaopen_lpeg is registered by the kernel.
# re.lua is embedded by //src/kernel/lua.
exports_files(["re.lua"])

cc_library(
    name = "lpeg",
    srcs = [
        "lpcap.c",
        "lpcode.c",
        "lpcset.c",
        "lpprint.c",
        "lptree.c",
        "lpvm.c",
        "lpcap.h",
        "lpcode.h",
        "lpcset.h",
        "lpprint.h",
        "lptree.h",
        "lptypes.h",
        "lpvm.h",
    ],
    copts = [
        "-w",
        "-std=gnu99",
    ],
    visibility = ["//visibility:public"],
    deps = ["//third_party/luajit"],
    alwayslink = True,
)
