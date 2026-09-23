# 17: Add rex_pcre2 to the build, so JSON Schema pattern/patternProperties work

Status: resolved
Category: enhancement
Type: task
Blocked by: none

## What

`third_party/lua-schema`'s `format.lua` and `keyword.lua` both do
`local has_pcre, pcre = pcall(require, 'rex_pcre2')` and degrade (or, for
`keyword.lua`'s `pcre_new`, raise `error('rex_pcre2 missing')`) when it is absent --
which it always is here: `qwe_lua_new()` (`src/kernel/luavm.c`) sets `package.path` and
`package.cpath` to `""` by design, so nothing loads from disk, and `rex_pcre2` is never
embedded. `third_party/lua-schema/json_schema_test_suite.lua` already documents this as a
known, deferred gap: it excludes `pattern.json` and `patternProperties.json` from the
conformance run with the comment `"pattern: rejected until PCRE2 is added"`.

Vendor PCRE2 (or a minimal build of it) and lrexlib's `rex_pcre2` C binding as a new
`third_party/` dependency, register its `luaopen_rex_pcre2` in `qwe_lua_new()`'s preload
table (`src/kernel/luavm.c`), the same way `lpeg`, `qwe.cbor`, `qwe.fs`, `qwe.exec` and
`qwe.secrets` already are -- no filesystem access, just a C function wired into
`package.preload`. Then un-exclude `pattern.json` and `patternProperties.json` in
`json_schema_test_suite.lua` and fix whatever those conformance cases now reveal.

## Acceptance criteria

- [x] `pattern.json` and `patternProperties.json` run (not skipped) in the conformance
      suite and pass: `unit: third_party/lua-schema:json_schema_test_suite`
- [x] A schema using `pattern:` validates a real regex against both matching and
      non-matching data, and the message on a mismatch is readable (not
      `rex_pcre2 missing`): `e2e:` a new case, or extended `validate_with_schema`
- [x] `bazel test //...` green; the coverage floor holds

## Comments

- Found while chasing an unrelated CI crash under `--config=release` (the LuaJIT `-O2`
  miscompilation fixed in this feature's 04/05 follow-up work). `rex_pcre2` being absent is
  not itself a bug -- its absence is handled, just incompletely, and the test suite already
  says so. See ticket 18 for the separate, more general problem that incident exposed: an
  unhandled Lua error escaping as an unprotected panic rather than failing gracefully.

### Resolution

- Vendored PCRE2 10.44 (`third_party/pcre2`: the 8-bit library only, JIT compiled in but
  never enabled, built from upstream's documented no-configure path -- `config.h.generic`,
  `pcre2.h.generic` and `pcre2_chartables.c.dist` copied in unchanged, HAVE_*/SUPPORT_*
  supplied as `copts`) and lrexlib 2.9.4's `rex_pcre2` binding, PCRE2 flavour only
  (`third_party/lrexlib`). `luaopen_rex_pcre2` is registered in `qwe_lua_new()`
  (`src/kernel/luavm.c`) the same way `lpeg` is.
- Un-excluded `pattern.json`/`patternProperties.json` in
  `third_party/lua-schema/json_schema_test_suite.lua`; all 793 cases in the suite pass
  (0 skips beyond `format.json` and `refRemote.json`, which are excluded for unrelated
  reasons).
- `pattern`/`patternProperties` are back in `src/kernel/schema/strict-metaschema.json`
  (removed by ADR-0009 "until a regex engine exists" -- it now does); `format` stays
  removed. `docs/adr/0009-schema-validation-runs-in-lua.md` and
  `src/kernel/schema/strict_meta_test.lua` updated to match.
- `plugins/builtin/file.ensure/schema.json`'s `mode` now has
  `"pattern": "^[0-7]{2,4}$"` (mirroring `plugin.lua`'s own `normalize_mode` check, just
  enforced earlier, at schema-validation time). `tests/e2e/validate_ok` already covers a
  matching mode ("0644"); the new `tests/e2e/validate_pattern_mismatch` covers a
  non-matching one, asserting the message is
  `must match pattern ^[0-7]{2,4}$`, not `rex_pcre2 missing`.
- **Found and fixed while running the OOM sweeps** (`src/cli/validate:oom_test`,
  `src/kernel:load_oom_test`): `kernel_validator` recompiles every builtin plugin's schema
  on every `qwe validate`/`qwe run` call, regardless of which plugins the workflow uses --
  so `file.ensure`'s new `pattern:` now runs through PCRE2 on every call. lua-schema's
  `core._new` implicitly self-validates every schema node it compiles against the
  registered draft-07 metaschema (separate from qwe's own strict one), which applies
  `format.lua`'s `format.regex` to any `pattern:` value; `format.regex`'s own `pcall`
  around `pcre_new` was written back when `pcre_new` was a shim that always threw
  `rex_pcre2 missing`; now that it can genuinely fail an allocation, that same `pcall`
  silently swallowed a real PCRE2/lrexlib out-of-memory error as if the pattern were just
  syntactically invalid -- one specific allocation number in the sweep reported a clean
  `qwe validate` success despite an allocation having actually failed. Fixed narrowly in
  `third_party/lua-schema/src/schema/format.lua`'s `format.regex`: re-raises when the
  caught error looks like an allocator failure (`malloc failed`, or mentions `memory`)
  instead of reporting it as an invalid pattern. `src/kernel/validate.c` gained
  `report_internal_error()`, shared by `qwe_validate_doc` and `qwe_validate_inventory`'s
  `internal:` fallbacks, so any such error that still escapes (a real bug) is reported
  as `<file>: out of memory ...` -- naming the file, like every other OOM path here --
  instead of a bare, fileless "internal error in the validator".
