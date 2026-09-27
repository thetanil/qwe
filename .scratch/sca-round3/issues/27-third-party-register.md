# 27: A register of the vendored components, for tool qualification

Status: ready-for-agent
Category: enhancement
Type: task

## What

Everything in `third_party/` ships in the binary or in the build that makes it,
and every static-analysis ticket so far excludes it ("vendored, upstream's to
fix"). That is not an answer for tool qualification. qwe is a software tool under
ISO 26262-8 clause 11, used for items up to ASIL D (ADR-0015), and the vendored
code is part of the tool. ISO 26262-8 clause 12 (qualifying software components)
does not apply, because that clause is for components of the item. Instead the
vendored code is covered by the tool's own qualification: under validation (1c)
the assessor asks what each component is, what it is used for, what its known
problems are, and what evidence exists, and its known anomalies go into the
tool's known-malfunction list. The README has a name/upstream/version/license table;
that is the start of a register.

| Component | Version | Linked into `qwe`? |
|---|---|---|
| libsodium | 1.0.20 | yes (secrets) |
| libyaml | 0.2.5 | yes (workflow parsing) |
| LuaJIT | v2.1 rolling, c6ffc141a876 | yes |
| TinyCBOR | v7.0 | yes |
| PCRE2 | 10.44 | yes (with lrexlib) |
| LPeg 1.1.0, lrexlib 2.9.4, dkjson 2.8, lua-schema, luacheck 1.2.0 | | yes (embedded Lua modules) |
| greatest 1.5.0, JSON-Schema-Test-Suite | | tests only |

Note "LuaJIT v2.1 rolling, commit c6ffc141a876": a rolling branch is a moving
target, and a specific commit is what the register must name.

## Fix

A `docs/third-party.md` with one section per component that is linked in:

- exact version and provenance (source, commit or tarball hash, how it came into
  `third_party/`, and whether any local patch exists: `git log third_party/<x>`);
- what qwe uses it for and which qwe files call it;
- **known anomalies:** the upstream's published vulnerability list and release
  notes for the version in use and every later one (CVE ids and fixed-in
  versions), and whether the version in use is affected. For LuaJIT's rolling
  commit, say how far behind upstream head it is;
- configuration: which features are compiled in and out (libyaml's,
  libsodium's minimal build, PCRE2's flags, LuaJIT's `LUAJIT_*`);
- **analysis we ran:** the same clang-tidy and analyzer configuration as `src/`
  run on `third_party/libyaml` and `third_party/tinycbor` (both small enough to
  read), reported as counts and reviewed, not fixed (upstream's to fix); and the
  compiler warnings and sanitizers we already run on them at runtime (ASan,
  UBSan, valgrind, the fuzz harnesses on the YAML path, per-file flags from `.bazelrc`);
- what would break if it were replaced, and what untrusted input reaches it
  (workflow YAML reaches libyaml and TinyCBOR; a plugin's Lua reaches LuaJIT;
  say which of these are trusted and why).

A test that fails when `third_party/` has a component with no section in
`docs/third-party.md` or a version that differs from the register.

`third_party/` is not edited.

## Acceptance criteria

- [ ] `docs/third-party.md` has a section per linked component with every item above.
      `manual: read it`
- [ ] The register's versions agree with `third_party/*/VERSION` and the README
      table, checked by a test. `unit: tools/ci/third_party_test.sh`
- [ ] The analysis counts for libyaml and tinycbor are recorded with the tool
      version, and they are reviewed for bugs that could affect qwe's use.
      `manual: Comments`
- [ ] For each component with a known vulnerability affecting the used version,
      a ticket exists (bump, patch by upstream release, or isolate). `manual: Comments`
- [ ] The README's "credits" table points to the register.
- [ ] `bazel test //...` is green.

## Comments
