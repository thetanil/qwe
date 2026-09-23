load("@rules_shell//shell:sh_test.bzl", "sh_test")

# Every fixture file the tests in this package read: the smoke workflows and negatives
# themselves, the project-plugin fixtures they load, and gen_secrets.sh's template.
filegroup(
    name = "smoke_sources",
    srcs = ["gen_secrets.sh", "smoke_secrets.yml.in"] + glob([
        "*.yml",
        ".qwe/plugins/**",
        "neg_plugin_top_level/**",
    ]),
)

# Runs every tests/smoke/*.yml (not neg_*.yml) with the fastbuild binary and expects exit 0,
# so the smoke workflows cannot rot between the pushes that actually exercise them in CI.
# Timing is not checked here, and a workflow marked "# smoke: skip-in-bazel: <reason>" (become,
# apt -- anything that needs privileges or real package installs the sandbox cannot give) is
# skipped: it still runs for real in the smoke GitHub job. Also runs gen_secrets.sh (11),
# which generates and runs smoke_secrets.yml from smoke_secrets.yml.in itself, needing
# neither privilege nor a real package: it is not skip-in-bazel.
sh_test(
    name = "smoke_workflows_test",
    size = "small",
    srcs = ["smoke_workflows_test.sh"],
    args = [
        "$(location //src/cli:qwe)",
        package_name(),
    ],
    data = ["//src/cli:qwe", ":smoke_sources"],
)

# 13: every built-in plugin (src/kernel/lua/BUILD's MODULES table) is used by some smoke
# workflow, and every smoke workflow/negative that the Bazel run above cannot itself verify
# (skip-in-bazel, or an unmarked neg_*) is named in .github/workflows/smoke.yml -- so a new
# file, or a new built-in plugin, cannot silently sit unused.
sh_test(
    name = "coverage_test",
    size = "small",
    srcs = ["coverage_test.sh"],
    data = [
        "//src/kernel/lua:BUILD",
        "//.github:ci_files",
        ":smoke_sources",
    ],
)
