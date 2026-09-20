# 15: Secrets: keygen, encrypt, decrypt late, redaction, secret outputs

Status: resolved
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

- [x] `qwe keygen` creates a 0600 file of 32 bytes (or base64 of 32 bytes), and a second call refuses and exits 2. `e2e: tests/e2e/keygen/` (with `HOME` pointed at a temporary directory)
- [x] A key file with mode 0644 is refused, with an error naming the file and its mode. `e2e: tests/e2e/key_perms_refused/`
- [x] `qwe encrypt` followed by decryption round-trips, and a tampered envelope fails authentication. `unit: src/secrets/envelope_test.c::roundtrip`, `::tamper_detected`
- [x] `qwe encrypt` doesn't accept the plaintext as an argument. `e2e: tests/e2e/encrypt_rejects_argv/`
- [x] `run: echo "$TOKEN"` with `env: { TOKEN: ${{ secrets.T }} }` logs `***`, in the terminal and in the log file. `e2e: tests/e2e/secret_redacted_env/`
- [x] `run: echo ${{ secrets.T }}` is a validation error at that position, and the message suggests `env:`. `e2e: tests/e2e/secret_in_run_text_rejected/`
- [x] A secret substituted into a larger `with:` string is treated as secret as a whole, and is redacted if printed. `e2e: tests/e2e/secret_taint_with/`
- [x] A `run:` step's `secret-outputs: [tok]` value, printed by a later step, is redacted. `e2e: tests/e2e/secret_output_redacted/`
- [x] A secret output never appears in `result.json`. `e2e: tests/e2e/secret_output_not_in_result/`
- [x] The same secret name in the workflow and the inventory is a validation error. `e2e: tests/e2e/secret_duplicate_name/`
- [x] The same secret name in the inventory-wide map and a target's map is a validation error for jobs on that target. `e2e: tests/e2e/secret_duplicate_target_scope/`
- [x] Two different targets may each define `SUDO`, and each job gets its own target's value. `e2e: tests/e2e/secret_per_target/`
- [x] Redaction still works when the secret is split across two pipe reads. `unit: src/kernel/redact_test.c::split_across_chunks`
- [x] A secret never appears in any child process's argv on either host (reads `/proc/*/cmdline` during the step, local and ssh). `e2e: tests/e2e/secret_not_in_argv/`, `tests/e2e/ssh_secret_not_in_argv/`

## Comments

- **libsodium 1.0.20** is vendored in `third_party/libsodium` (the release's `src/libsodium`, unchanged except the generated `version.h`; tarball sha256 in the BUILD file). Only the portable C is built: no `HAVE_*` SIMD macros, so it uses its reference implementations.
- **Where things live.** `src/secrets/envelope.c` (seal/open/check) and `keyfile.c` (load with the permission check, generate with `O_EXCL`); `src/kernel/redact.c` (the run-wide set and the per-stream redactor); `src/kernel/luasecrets.c` (`qwe.secrets`: `reveal`, `mask`, `check`); `qwe keygen` and `qwe encrypt` in `src/cli`. The file key source is a C module, not a Lua service plugin: it has nothing to be pluggable over yet, and the seam (`qwe_key_load`) is where a second source would go.
- **Envelope text form:** `qwe:1:xchacha20poly1305:<base64(nonce || ciphertext || tag)>`. The version and algorithm id are the AEAD's additional data. `qwe encrypt` drops one trailing newline of its input (so `echo x | qwe encrypt` seals `x`; use `printf %s` to be exact).
- **The two decisions from ticket 02.** The tag is `QWE_SECRET_TAG` = 32768, one header (`secret_tag.h`) shared by C and `qwe.cbor`. The `with: type: string` question needs no custom keyword: a `with:` value that uses a secret is the text `${{ secrets.X }}`, a string, when it is validated; a bare `!encrypted` in `with:` is refused (an envelope is only allowed as an entry of a `secrets:` map).
- **Validation.** `secrets:` entries must be `!encrypted` with a well-formed envelope (checked without the key, so `qwe validate` needs none). A name defined in the workflow and the inventory is an error at the workflow's key. A name defined in the target's map and in the workflow's or inventory-wide map is an error at the `target:` of each job that uses that target. `${{ secrets.X }}` needs `X` visible to the job; in `run:` text it is an error that suggests `env:`.
- **Decryption** is in the parent, in `qwe.template.resolve`, when a step's values are made, and the key file is read then (a workflow without secrets never needs one). A refused key file or a failed authentication fails the step's start (`engine-error`), naming the file and the mode.
- **Redaction** is in `drain()`, before the ring: a redactor holds back only a tail that is a proper prefix of a known secret, so ordinary output is not delayed, and flushes at the step's end. Secrets added later (a secret output) apply to later bytes only, which is all the parent can do.
- **Taint.** A string built from `${{ secrets.X }}` (an `env:` value or a `with:` string, also one that reads such an env value) is added to the redaction set as a whole. As a side effect `${{ env.X }}` in `run:`/`with:` text now reads the finished env, with `steps`, `vars` and `secrets` already substituted.
- **Secret outputs.** `qwe.template.store` takes the step: an output is secret if the step lists it in `secret-outputs:` or its plugin schema says `secret: true`. It is masked as soon as the parent stores it, stays readable by later steps of the job, and is left out of `result.json`.
- **Limits.** Lua strings cannot be zeroed, so `sodium_memzero` covers the C buffers (key, decrypted plaintext, the `encrypt` input) but not the copies Lua holds. Redaction is exact-match on the plaintext: base64 or other encodings of a secret are not masked.
- **Tests.** `envelope_test` (roundtrip, tamper, key file modes), `redact_test` (`split_across_chunks` and others), and 13 e2e cases; cases use a `home/` with a committed test key (base64), `HOME=./home` in `env`, and a new `setup.sh` hook in `run_case.sh` to set modes. `ssh_secret_not_in_argv` skips without the ssh target.
- `bazel test //...` green (134 tests).
