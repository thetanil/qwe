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

e2e_test(name = "parallel_rendezvous")

e2e_test(name = "parallel_limit_enforced")

e2e_test(name = "parallel_failure_isolation")

e2e_test(name = "parallel_cancel")

e2e_test(name = "parallel_log_separation")

# Deliberately wrong RUN/ golden: proves goldens beyond stdout are compared
# (under bazel they are symlinks in runfiles).
e2e_test(
    name = "harness_selftest_run_golden",
    expect_mismatch = True,
)

e2e_test(name = "validate_bad_job_id")

e2e_test(name = "validate_bad_step_id")

e2e_test(name = "run_refuses_bad_id")

e2e_test(name = "trace_transitions")

e2e_test(name = "trace_debug")

e2e_test(name = "engine_error_spawn")

e2e_test(name = "straggler_after_success")

e2e_test(name = "grace_outlives_leader")

e2e_test(name = "harness_selftest_signal_on_file")

e2e_test(name = "builtin_no_disk")

e2e_test(name = "project_plugin_loads")

e2e_test(name = "project_plugin_no_shadowing")

e2e_test(name = "strict_globals_write")

e2e_test(name = "strict_globals_read")

e2e_test(name = "plugin_schema_metaschema")

e2e_test(name = "plugin_contract_missing_apply")

e2e_test(name = "plugin_contract_output_flags")

e2e_test(name = "plugin_luacheck")

e2e_test(name = "file_ensure_idempotent")

e2e_test(name = "not_converged")

e2e_test(name = "plugin_segfault_isolated")

e2e_test(name = "plugin_block_timeout")

e2e_test(name = "run_always_changed")

e2e_test(name = "env_scopes")

e2e_test(name = "env_not_in_argv")

e2e_test(name = "run_outputs")

e2e_test(name = "stdout_is_not_output")

e2e_test(name = "outputs_job_scoped")

e2e_test(name = "no_runtime_env_carryover")

e2e_test(name = "output_file_cleaned")

e2e_test(name = "plugin_outputs_flow")

e2e_test(name = "validate_unknown_context")

e2e_test(name = "validate_unknown_output_key")

e2e_test(name = "inventory_default_location")

e2e_test(name = "inventory_flag_wins")

e2e_test(name = "unknown_target")

e2e_test(name = "local_needs_no_inventory")

e2e_test(name = "inventory_local_reserved")

e2e_test(name = "inventory_unknown_backend")

e2e_test(name = "inventory_ssh_requires_host")

e2e_test(name = "vars_per_target")

e2e_test(name = "vars_unknown_key")

e2e_test(name = "vars_not_auto_exported")

e2e_test(name = "vars_in_run_text")

e2e_test(name = "envelope_only_in_secrets")

e2e_test(name = "inventory_no_inheritance_keys")

e2e_test(name = "ssh_hostname")

e2e_test(name = "ssh_one_master_sequential")

e2e_test(name = "ssh_one_master_parallel")

e2e_test(name = "ssh_parallel_rendezvous")

e2e_test(name = "ssh_session_cap")

e2e_test(name = "ssh_timeout_kills_remote")

e2e_test(name = "ssh_connection_lost")

e2e_test(name = "ssh_master_closed")

e2e_test(name = "ssh_env_and_stdin")

e2e_test(name = "on_local_override")

e2e_test(name = "ssh_become_denied")

e2e_test(name = "local_become")

e2e_test(name = "validate_become_type")

e2e_test(name = "keygen")

e2e_test(name = "key_perms_refused")

e2e_test(name = "encrypt_rejects_argv")

e2e_test(name = "secret_redacted_env")

e2e_test(name = "secret_in_run_text_rejected")

e2e_test(name = "secret_taint_with")

e2e_test(name = "secret_output_redacted")

e2e_test(name = "secret_output_not_in_result")

e2e_test(name = "secret_duplicate_name")

e2e_test(name = "secret_duplicate_target_scope")

e2e_test(name = "secret_per_target")

e2e_test(name = "secret_not_in_argv")

e2e_test(name = "ssh_secret_not_in_argv")

e2e_test(name = "secret_outputs_multiple")
