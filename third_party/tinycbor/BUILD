load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# TinyCBOR v7.0 (intel/tinycbor, 6442e749ca81), MIT. The two headers CMake
# would generate (tinycbor-version.h, tinycbor-export.h) are written by hand.
# Pretty-printer and JSON conversion are left out: they need stdio and
# open_memstream.
cc_library(
    name = "tinycbor",
    srcs = [
        "src/cborencoder.c",
        "src/cborencoder_close_container_checked.c",
        "src/cborencoder_float.c",
        "src/cborerrorstrings.c",
        "src/cborinternal_p.h",
        "src/cborinternalmacros_p.h",
        "src/cborparser.c",
        "src/cborparser_dup_string.c",
        "src/cborparser_float.c",
        "src/cborvalidation.c",
        "src/compilersupport_p.h",
        "src/memory.h",
        "src/tinycbor-export.h",
        "src/tinycbor-version.h",
        "src/utf8_p.h",
    ],
    hdrs = [
        "src/cbor.h",
        "src/tinycbor-export.h",
        "src/tinycbor-version.h",
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "roundtrip_test",
    size = "small",
    srcs = ["roundtrip_test.c"],
    deps = [
        ":tinycbor",
        "//third_party/greatest",
    ],
)
