load(":defs.bzl", "e2e_test", "e2e_valgrind_test")
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

e2e_test(name = "validate_errors_in_position_order")
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

e2e_test(name = "luacov_child_process")

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

e2e_test(name = "job_select_closure")

e2e_test(name = "job_select_multiple")

e2e_test(name = "job_select_unknown")

e2e_test(name = "job_select_validates_all")
e2e_test(name = "validate_duplicate_key")
e2e_test(name = "validate_duplicate_key_nested")
e2e_test(name = "inventory_duplicate_key")
e2e_test(name = "validate_timeout_minutes_renamed")
e2e_test(name = "validate_timeout_too_large")
e2e_test(name = "step_timeout_rounds_up")
e2e_test(name = "output_file_is_private")
e2e_test(name = "ssh_socket_dir_refused")
e2e_test(name = "ssh_master_expires_after_kill")
e2e_test(name = "ssh_stale_socket_cleaned")
e2e_test(name = "ssh_preconnect_timeout_is_exact")
e2e_test(name = "ssh_unreachable_does_not_stall")
e2e_test(name = "ssh_preconnect_runs_first")
e2e_test(name = "ssh_preconnect_only_selected")
e2e_test(name = "ssh_unreachable_fails_its_jobs")
e2e_test(name = "ssh_teardown_does_not_block")
e2e_test(name = "ssh_wedged_socket_does_not_hang")
e2e_test(name = "plugin_top_level_refused")
e2e_test(name = "inventory_target_disabled")
e2e_test(name = "inventory_target_disabled_not_contacted")
e2e_test(name = "validate_ignores_disabled")
e2e_test(name = "inventory_disabled_must_be_a_reason")

# The valgrind gate (quality/04): a chosen set of cases, chosen for the paths
# the unit tests cannot reach. Not the ssh cases: they need a reachable host.
#   bazel test //tests/e2e:valgrind_e2e
[e2e_valgrind_test(name = c) for c in [
    "run_outputs",  # a plain run, plugin-free
    "file_ensure_idempotent",  # a run with a plugin
    "secret_redacted_env",  # secrets and redaction
    "operator_cancel",  # cancellation and teardown
    "step_timeout_fails",  # a timeout
    "needs_skip_chain",  # a needs: chain
]]

test_suite(
    name = "valgrind_e2e",
    tags = ["manual"],
    tests = [
        ":file_ensure_idempotent_valgrind_test",
        ":needs_skip_chain_valgrind_test",
        ":operator_cancel_valgrind_test",
        ":run_outputs_valgrind_test",
        ":secret_redacted_env_valgrind_test",
        ":step_timeout_fails_valgrind_test",
    ],
)
e2e_test(name = "validate_secret_not_visible")
e2e_test(name = "validate_vars_at_workflow_level")
e2e_test(name = "validate_env_names")
e2e_test(name = "validate_become_value")
e2e_test(name = "inventory_bad_names")
e2e_test(name = "plugin_files_malformed")
e2e_test(name = "validate_unknown_target")
e2e_test(name = "validate_step_run_and_uses")
e2e_test(name = "plugin_shape_and_array_args")

# Every workflow and inventory the e2e cases use: the seed corpus for the YAML fuzzers.
filegroup(
    name = "yaml_seeds",
    testonly = True,
    srcs = glob([
        "*/w.yaml",
        "*/inventory.yaml",
    ]),
    visibility = ["//src/edge/yaml:__pkg__"],
)
