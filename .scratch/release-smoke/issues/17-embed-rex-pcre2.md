# 17: Add rex_pcre2 to the build, so JSON Schema pattern/patternProperties work

Status: ready-for-agent
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

- [ ] `pattern.json` and `patternProperties.json` run (not skipped) in the conformance
      suite and pass: `unit: third_party/lua-schema:json_schema_test_suite`
- [ ] A schema using `pattern:` validates a real regex against both matching and
      non-matching data, and the message on a mismatch is readable (not
      `rex_pcre2 missing`): `e2e:` a new case, or extended `validate_with_schema`
- [ ] `bazel test //...` green; the coverage floor holds

## Comments

- Found while chasing an unrelated CI crash under `--config=release` (the LuaJIT `-O2`
  miscompilation fixed in this feature's 04/05 follow-up work). `rex_pcre2` being absent is
  not itself a bug -- its absence is handled, just incompletely, and the test suite already
  says so. See ticket 18 for the separate, more general problem that incident exposed: an
  unhandled Lua error escaping as an unprotected panic rather than failing gracefully.
