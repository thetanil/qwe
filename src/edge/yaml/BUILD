load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "yaml",
    srcs = [
        "positions.c",
        "transcode.c",
    ],
    hdrs = [
        "positions.h",
        "secret_tag.h",
        "transcode.h",
        "transcode_hooks.h",
    ],
    visibility = ["//src:__subpackages__"],
    deps = [
        "//third_party/libyaml",
        "//third_party/tinycbor",
    ],
)

[cc_test(
    name = name + "_test",
    size = "small",
    srcs = [name + "_test.c"],
    deps = [
        ":yaml",
        "//third_party/greatest",
        "//third_party/tinycbor",
    ],
) for name in [
    "limits",
    "magic",
    "positions",
    "tags",
    "transcode",
]]

# Wraps the allocator to fail one call at a time.
cc_test(
    name = "alloc_test",
    size = "small",
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    srcs = ["alloc_test.c"],
    linkopts = [
        "-Wl,--wrap=malloc",
        "-Wl,--wrap=realloc",
        "-Wl,--wrap=calloc",
    ],
    deps = [
        ":yaml",
        "//third_party/greatest",
    ],
)

# Fuzzing (quality/06). The harness is a library so the corpus replay in the
# normal suite runs exactly what the fuzzers run.
cc_library(
    name = "fuzz_harness",
    testonly = True,
    srcs = ["fuzz_harness.c"],
    hdrs = ["fuzz_harness.h"],
    deps = [
        ":yaml",
        "//src/kernel",
        "//src/kernel:luacbor",
        "//src/kernel:luavm",
        "//third_party/luajit",
        "//third_party/tinycbor",
    ],
)

# Regression corpus (crashes the fuzzer found, plus hand-written edge inputs) and the e2e workflows that seed it.
cc_test(
    name = "corpus_test",
    size = "small",
    srcs = ["corpus_test.c"],
    args = ["$(locations :corpus_files)", "$(locations //tests/e2e:yaml_seeds)"],
    data = [
        ":corpus_files",
        "//tests/e2e:yaml_seeds",
    ],
    deps = [
        ":fuzz_harness",
        "//third_party/greatest",
    ],
)

filegroup(
    name = "corpus_files",
    srcs = glob(["corpus/*"]),
)

exports_files(["qwe.dict"])

# libFuzzer targets. Manual: build them with
#   bazel build --config=fuzz --config=asan //src/edge/yaml:transcode_fuzz //src/edge/yaml:chain_fuzz
# and run them through tools/fuzz/run.sh.
[cc_binary(
    name = name + "_fuzz",
    testonly = True,
    srcs = [name + "_fuzz.c"],
    copts = ["-fsanitize=fuzzer-no-link"],
    linkopts = ["-fsanitize=fuzzer"],
    tags = ["manual"],
    deps = [":fuzz_harness"],
) for name in [
    "transcode",
    "chain",
]]
