load(":defs.bzl", "e2e_test")

exports_files(["run_case.sh", "invert.sh"])

e2e_test(name = "cli_unknown_subcommand")

e2e_test(name = "cli_version")

e2e_test(name = "cli_serve_stub")

# Deliberately wrong golden; passes only because the harness reports a mismatch.
e2e_test(
    name = "harness_selftest_mismatch",
    expect_mismatch = True,
)
