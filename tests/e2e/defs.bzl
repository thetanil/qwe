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

def e2e_valgrind_test(name):
    """Runs tests/e2e/<name>/ as <name>_valgrind_test, with qwe and the step children it forks under valgrind.

    Tagged manual: //... does not build it. The valgrind_e2e test_suite names the set.

    Args:
      name: case directory name (the case is shared with the plain <name>_test).
    """
    case_files = native.glob([name + "/**"], allow_empty = False)
    sh_test(
        name = name + "_valgrind_test",
        size = "large",
        srcs = ["valgrind_case.sh"],
        args = [
            "$(location :run_case.sh)",
            "$(location //tools/valgrind:qwe_under_valgrind.sh)",
            "$(location //src/cli:qwe)",
            "$(location //tools/valgrind:luajit.supp)",
            native.package_name() + "/" + name,
        ],
        data = [
            ":run_case.sh",
            "//src/cli:qwe",
            "//tools/valgrind:qwe_under_valgrind.sh",
            "//tools/valgrind:luajit.supp",
        ] + case_files,
        tags = ["manual", "valgrind"],
    )
