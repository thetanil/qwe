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

e2e_test(name = "tracer_hello")

e2e_test(name = "tracer_exit_code")

e2e_test(name = "tracer_stderr_merged")

e2e_test(name = "tracer_large_output")
e2e_test(name = "validate_malformed_yaml")

e2e_test(name = "validate_ok")

e2e_test(name = "validate_unknown_key")

e2e_test(name = "validate_with_schema")

e2e_test(name = "validate_union_single_error")

e2e_test(name = "validate_duplicate_step_id")

e2e_test(name = "validate_on_only_local")

e2e_test(name = "validate_target_required")

e2e_test(name = "validate_unknown_plugin")

e2e_test(name = "run_refuses_invalid")

e2e_test(name = "steps_in_order")

e2e_test(name = "step_failure_stops_job")

e2e_test(name = "continue_on_error")

e2e_test(name = "needs_skip_chain")

e2e_test(name = "step_timeout_fails")

e2e_test(name = "step_timeout_continue")

e2e_test(name = "job_timeout_cancels")

e2e_test(name = "grace_then_sigkill")

e2e_test(name = "operator_cancel")
