load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

# Set by --config=valgrind in .bazelrc (--define=qwe_valgrind=1).
config_setting(
    name = "valgrind",
    define_values = {"qwe_valgrind": "1"},
    visibility = ["//visibility:public"],
)

exports_files([
    "luajit.supp",
    "run_under.sh",
    "qwe_under_valgrind.sh",
])

sh_binary(
    name = "run_under",
    srcs = ["run_under.sh"],
    data = ["luajit.supp"],
    visibility = ["//visibility:public"],
)
