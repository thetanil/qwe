load("@rules_shell//shell:sh_test.bzl", "sh_test")

exports_files(["check_version.sh"])

sh_test(
    name = "check_version_test",
    size = "small",
    srcs = ["check_version_test.sh"],
    args = ["$(location :check_version.sh)"],
    data = [":check_version.sh"],
)
