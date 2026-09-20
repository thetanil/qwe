"""e2e_test: run the built qwe binary on a case directory and diff against goldens."""

load("@rules_shell//shell:sh_test.bzl", "sh_test")

def e2e_test(name, expect_mismatch = False, tags = [], data = []):
    """Runs tests/e2e/<name>/ as test <name>_test (the suffix keeps the test binary from shadowing the case dir in runfiles).

    Args:
      name: case directory name, also the test name.
      expect_mismatch: run under a wrapper that passes only if the goldens differ.
      tags: extra tags.
      data: extra data files.
    """
    case_files = native.glob([name + "/**"], allow_empty = False)
    if expect_mismatch:
        sh_test(
            name = name + "_test",
            size = "small",
            srcs = ["invert.sh"],
            args = ["$(location :run_case.sh)", "$(location //src/cli:qwe)", native.package_name() + "/" + name],
            data = [":run_case.sh", "//src/cli:qwe"] + case_files + data,
            tags = tags,
        )
    else:
        sh_test(
            name = name + "_test",
            size = "small",
            srcs = ["run_case.sh"],
            args = ["$(location //src/cli:qwe)", native.package_name() + "/" + name],
            data = ["//src/cli:qwe"] + case_files + data,
            tags = tags,
        )
