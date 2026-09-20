load("@rules_cc//cc:defs.bzl", "cc_library")

# libyaml 0.2.5 (yaml/libyaml), MIT. Built with the version macros that its
# autotools/CMake build would pass on the command line.
cc_library(
    name = "libyaml",
    srcs = [
        "src/api.c",
        "src/dumper.c",
        "src/emitter.c",
        "src/loader.c",
        "src/parser.c",
        "src/reader.c",
        "src/scanner.c",
        "src/writer.c",
        "src/yaml_private.h",
    ],
    hdrs = ["include/yaml.h"],
    copts = [
        "-DYAML_VERSION_MAJOR=0",
        "-DYAML_VERSION_MINOR=2",
        "-DYAML_VERSION_PATCH=5",
        '-DYAML_VERSION_STRING=\\"0.2.5\\"',
        "-D_POSIX_C_SOURCE=200809L",
        "-Wno-error",
        "-Wno-unused-parameter",
        "-Wno-sign-compare",
        "-Wno-unused-value",  # emitter.c's PUT_BREAK/WRITE_BREAK macros: upstream, harmless
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
