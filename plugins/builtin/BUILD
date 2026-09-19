load("@rules_cc//cc:defs.bzl", "cc_library")

genrule(
    name = "gen_embedded",
    srcs = [
        "backend_local/local.lua",
        "run/run.lua",
    ],
    outs = ["embedded.c"],
    cmd = "$(location //third_party/luajit:minilua) $(location //tools:embed.lua) $@ " +
          "backend.local=$(location backend_local/local.lua) " +
          "run=$(location run/run.lua)",
    tools = [
        "//third_party/luajit:minilua",
        "//tools:embed.lua",
    ],
)

cc_library(
    name = "builtin",
    srcs = [":gen_embedded"],
    hdrs = ["embedded.h"],
    visibility = ["//src:__subpackages__"],
)
