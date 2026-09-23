load("@rules_cc//cc:defs.bzl", "cc_library")

# lrexlib 2.9.4 (rrthomas/lrexlib), MIT/X11. Only the PCRE2 binding
# (rex_pcre2) is vendored; the GNU, oniguruma, POSIX, PCRE1 and TRE flavours
# are not needed and are not included. luaopen_rex_pcre2 is registered by the
# kernel (src/kernel/luavm.c), the same way lpeg is.
#
# algo.h is a template of Lua C functions shared by every lrexlib flavour;
# lpcre2.c #includes it directly; it is never compiled on its own.
cc_library(
    name = "rex_pcre2",
    srcs = [
        "src/common.c",
        "src/pcre2/lpcre2.c",
        "src/pcre2/lpcre2_f.c",
    ],
    hdrs = [
        "src/algo.h",
        "src/common.h",
    ],
    copts = [
        "-w",
        "-std=gnu99",
        # Upstream's rockspecs.lua defines these for every lrexlib flavour
        # (see rockspecs.lua's shared `defines` table); there is no configure
        # step to generate them from.
        '-DVERSION=\\"2.9.4\\"',
        "-DLUA_LIB=",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//third_party/luajit",
        "//third_party/pcre2",
    ],
    alwayslink = True,
)
