load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

# The perf gate (docs/adr/0016-perf-gate-compares-with-stored-expectations.md):
#   bazel run //tools/perf:compare -- <expected.tsv> <samples.tsv> <allow-list> <outdir>
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

# The candidate's sample collector: bazel run //tools/perf:run --
#   <outdir> <rounds> <candidate-bin> <workflow-file>...
sh_binary(
    name = "run",
    srcs = ["run.sh"],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "run_test",
    size = "small",
    srcs = ["run_test.sh"],
    args = ["$(location :run.sh)"],
    data = [":run.sh"],
)

# Pools samples into expected values: bazel run //tools/perf:expect --
#   [--note TEXT]... <version> <out.tsv> <samples.tsv>[:bin,bin...]...
sh_binary(
    name = "expect",
    srcs = ["expect.sh"],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "expect_test",
    size = "small",
    srcs = ["expect_test.sh"],
    args = ["$(location :expect.sh)"],
    data = [":expect.sh"],
)

# The stored expected values cover every workflow in workflows.txt, the list the perf job
# and .github/workflows/perf-baseline.yml time.
sh_test(
    name = "expected_test",
    size = "small",
    srcs = ["expected_test.sh"],
    data = [
        "expected.tsv",
        "workflows.txt",
        "//tests/smoke:smoke_sources",
    ],
)
