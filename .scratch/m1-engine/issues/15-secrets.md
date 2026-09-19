# 15: Secrets: keygen, encrypt, decrypt late, redaction, secret outputs

Status: ready-for-agent
Type: task
Blocked by: 11, 12

## What

- **Vendor libsodium.**
- **`qwe keygen`:** writes `~/.config/qwe/secret` with mode 0600 and refuses to overwrite an existing file.
- **`qwe encrypt`:** reads the plaintext from stdin and writes a versioned envelope using XChaCha20-Poly1305.
- **The file key source** is a service plugin that refuses a key file with group or world permissions.
- **`secrets:` maps** in the workflow, the inventory (inventory-wide) and each inventory target. A job sees the workflow's, the inventory-wide and its own target's secrets. The same name in two of those is an error.
- **`${{ secrets.X }}`** is allowed in `env:` and `with:`. It's a validation error inside `run:` text.
- **Decryption** happens in the parent just before the step that needs the value.
- **Run-wide redaction** at the tee, before the ring's consumers.
- **Secret outputs.** A plugin schema can mark an output `secret: true`, and a `run:` step can list `secret-outputs:`. The plaintext joins the redaction set as soon as the parent receives it.
- **Memory hygiene.** Plaintext buffers are zeroed with `sodium_memzero` after use.
- **Two open decisions from ticket 02, to settle before starting this ticket:**
  - the secret CBOR tag number (RFC 8949's first-come-first-served range, ≥ 32768), defined in one header shared by C and `qwe.cbor`,
  - how a secret-typed value satisfies a `with:` schema that says `type: string`. Options are a custom lua-schema keyword, a custom type, or a placeholder string substituted before validation.

## Acceptance criteria

- [ ] `qwe keygen` creates a 0600 file of 32 bytes (or base64 of 32 bytes), and a second call refuses and exits 2. `e2e: tests/e2e/keygen/` (with `HOME` pointed at a temporary directory)
- [ ] A key file with mode 0644 is refused, with an error naming the file and its mode. `e2e: tests/e2e/key_perms_refused/`
- [ ] `qwe encrypt` followed by decryption round-trips, and a tampered envelope fails authentication. `unit: src/secrets/envelope_test.c::roundtrip`, `::tamper_detected`
- [ ] `qwe encrypt` doesn't accept the plaintext as an argument. `e2e: tests/e2e/encrypt_rejects_argv/`
- [ ] `run: echo "$TOKEN"` with `env: { TOKEN: ${{ secrets.T }} }` logs `***`, in the terminal and in the log file. `e2e: tests/e2e/secret_redacted_env/`
- [ ] `run: echo ${{ secrets.T }}` is a validation error at that position, and the message suggests `env:`. `e2e: tests/e2e/secret_in_run_text_rejected/`
- [ ] A secret substituted into a larger `with:` string is treated as secret as a whole, and is redacted if printed. `e2e: tests/e2e/secret_taint_with/`
- [ ] A `run:` step's `secret-outputs: [tok]` value, printed by a later step, is redacted. `e2e: tests/e2e/secret_output_redacted/`
- [ ] A secret output never appears in `result.json`. `e2e: tests/e2e/secret_output_not_in_result/`
- [ ] The same secret name in the workflow and the inventory is a validation error. `e2e: tests/e2e/secret_duplicate_name/`
- [ ] The same secret name in the inventory-wide map and a target's map is a validation error for jobs on that target. `e2e: tests/e2e/secret_duplicate_target_scope/`
- [ ] Two different targets may each define `SUDO`, and each job gets its own target's value. `e2e: tests/e2e/secret_per_target/`
- [ ] Redaction still works when the secret is split across two pipe reads. `unit: src/kernel/redact_test.c::split_across_chunks`
- [ ] A secret never appears in any child process's argv on either host (reads `/proc/*/cmdline` during the step, local and ssh). `e2e: tests/e2e/secret_not_in_argv/`, `tests/e2e/ssh_secret_not_in_argv/`
