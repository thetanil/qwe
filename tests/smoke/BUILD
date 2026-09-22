load("@rules_shell//shell:sh_test.bzl", "sh_test")

# Runs every tests/smoke/*.yml (not neg_*.yml) with the fastbuild binary and expects exit 0,
# so the smoke workflows cannot rot between the pushes that actually exercise them in CI.
# Timing is not checked here, and a workflow marked "# smoke: skip-in-bazel: <reason>" (become,
# apt -- anything that needs privileges or real package installs the sandbox cannot give) is
# skipped: it still runs for real in the smoke GitHub job.
sh_test(
    name = "smoke_workflows_test",
    size = "small",
    srcs = ["smoke_workflows_test.sh"],
    args = [
        "$(location //src/cli:qwe)",
        package_name(),
    ],
    data = ["//src/cli:qwe"] + glob(["*.yml"]),
)
