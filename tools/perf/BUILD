load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

# The perf A/B gate: bazel run //tools/perf:compare -- <baseline-version> <samples.tsv> <allow-list> <outdir>
sh_binary(
    name = "compare",
    srcs = ["compare.sh"],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "compare_test",
    size = "small",
    srcs = ["compare_test.sh"],
    args = ["$(location :compare.sh)"],
    data = [":compare.sh"],
)
