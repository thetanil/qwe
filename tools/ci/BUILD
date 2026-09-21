load("@rules_shell//shell:sh_test.bzl", "sh_test")

# Fails when the workflows, the README badges and docs/ci-checks.md drift apart.
sh_test(
    name = "workflows_test",
    size = "small",
    srcs = ["workflows_test.sh"],
    data = [
        "//:README.md",
        "//.github:ci_files",
        "//docs:ci-checks.md",
    ],
)
