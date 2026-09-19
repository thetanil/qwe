# Research: CBOR library and JSON Schema validator for qwe (ticket 02)

Date: 2026-09-19. Scope: `.scratch/m1-engine/issues/02-choose-cbor-and-json-schema.md`.
Constraints taken from `docs/workflow-kernel-design.md` §2.3, §2.4, §3.2, §3.3, §6, §15, §16 and `CLAUDE.md`. In short: C99 or pure Lua on LuaJIT, a permissive license, no build-time code generation, and a plain Bazel `cc_library`.

Method. Every candidate was cloned with `git clone --depth 1` (or downloaded as a release tarball or rock) into
`/home/user/.claude/jobs/bfa23448/tmp/research/<owner>_<repo>/`. That scratch area is outside the repo and may be cleaned up. For each candidate I read the LICENSE file, the source and the build files. Release dates come from the GitHub releases API (`api.github.com/repos/<o>/<r>/releases`) or from the project's own CHANGES file. I compiled the C candidates with `gcc -std=c99 -Wall -Wextra [-pedantic]`. I ran the Lua candidates on a LuaJIT 2.1 built from source (`LuaJIT/LuaJIT` branch `v2.1`, commit `c6ffc141a876`, 2026-09-08). The spot-check scripts are summarized in §8. No Python was used.

---

## 1. Recommendation

### 1.1 CBOR in C: **TinyCBOR (intel/tinycbor), tag `v7.0`**, MIT

TinyCBOR is MIT-licensed ([LICENSE](https://github.com/intel/tinycbor/blob/main/LICENSE)). Its CMake file forces the library to compile as C99 with extensions off (`C_STANDARD 99`, `C_EXTENSIONS OFF`, `CMakeLists.txt` @ `bac6648`). It is the reference C implementation Intel maintains, and 7.0 is the first release to drop the "0." prefix because "this is not beta code" ([v7.0 release, 2026-02-18](https://github.com/intel/tinycbor/releases/tag/v7.0)). The core never allocates: the caller supplies the output buffer, or a writer callback (`cbor_encoder_init_writer`, `src/cbor.h:242`). Tags have first-class API calls: `cbor_encode_tag` (`src/cbor.h:247`) and `cbor_value_get_tag` / `cbor_value_skip_tag`.

The decoder is a small, copyable cursor (`CborValue`). That suits a validator that has to rewind for `oneOf`, and it suits a Lua converter. The encoder supports indefinite-length maps and arrays (`CborIndefiniteLength`). The YAML transcoder needs exactly that, because libyaml events do not announce child counts, and byte offsets of already-written items never shift.

Building it with Bazel needs two tiny headers that CMake would normally generate. Both can be hand-written (see §2.1). I compiled the library with `gcc -std=c99 -Wall -Wextra -Werror` and it builds cleanly. A round-trip of a map, an array, a byte string and a custom tag passed (§8).

### 1.2 CBOR on the Lua side: **our own small C Lua module over TinyCBOR** (`qwe.cbor`), not FFI and not a second CBOR library

The kernel needs C code that converts CBOR to Lua anyway. The parent has to push validated `with:` inputs into the Lua state, and it has to hand the workflow to the validator (see §5). The same file can also expose `encode`/`decode` to plugins through the ordinary Lua C API. The forked child inherits it for free, because it is compiled into the binary.

This keeps a single CBOR implementation. It also lets qwe define the Lua mapping itself, which no off-the-shelf Lua library gets right for us:
- text strings versus byte strings,
- the secret tag as a distinct Lua type,
- arrays versus objects,
- null.

It is about 300–400 lines of C99 that we write ourselves.

LuaJIT FFI directly over TinyCBOR is a poor fit. 59 TinyCBOR API functions are `static inline` (`grep -c CBOR_INLINE_API src/cbor.h` = 59; `CBOR_INLINE_API` expands to `static inline`, `src/cbor.h:71`), including `cbor_value_get_tag`. FFI cannot call those, and the struct layouts would have to be copied into `ffi.cdef`.

If a pure-Lua fallback is ever wanted, **fperrad/lua-ConciseSerialization 0.2.4 (MIT)** is the one to use:
- it encodes Lua strings as text strings by default (`set_string'text_string'`, `src/CBOR.lua:879`),
- its tag hook round-trips our secret tag (tested, §8),
- but it drops any tag without a registered builder without saying so (tested).

Zash lua-cbor encodes every Lua string, including map keys, as a CBOR byte string. It also mis-decodes some doubles on LuaJIT (tested, §3.2).

### 1.3 JSON Schema: **fperrad/lua-schema (pure Lua, MIT), pinned to commit `1a14a04c8586` (post-0.1.2), using draft-07, plus a qwe "strict" metaschema**, running on the parent's LuaJIT state

- **Why it wins.** It is the only candidate found that meets all of the following:
  - pure Lua 5.1-compatible (runs on LuaJIT, tested) with a permissive license (MIT, `LICENSE`),
  - actively maintained (last commit 2026-09-18),
  - supports drafts 4, 6, 7, 2019-09 and 2020-12 (`docs/index.md`; listed on json-schema.org's tooling data: [tooling-data.yaml](https://github.com/json-schema-org/website/blob/main/data/tooling-data.yaml), entry `lua-schema`),
  - has `const`, `oneOf`, `$ref`, `$defs`/`definitions`, `additionalProperties`, `required`, `enum`, `type` and `pattern`,
  - returns standard JSON Schema output with **`instanceLocation` as a JSON Pointer** (`docs/schema.md`; tested: `/steps/1/with/pth`),
  - ships the draft metaschemas as Lua tables, so no JSON parser is needed for them (`src/schema/draft-07.lua`).
- **Why draft-07.** It has `const` (added in draft-06: [draft-06 release notes](https://json-schema.org/draft-06/json-schema-release-notes)). Its metaschema is one self-contained document that recurses through `{"$ref": "#"}` ([draft-07 schema](https://json-schema.org/draft-07/schema)). That makes the "catch `requried`" trick in §4 a one-line change. Draft 2020-12 splits its metaschema into seven vocabulary files joined by `$dynamicRef` ([2020-12 schema](https://json-schema.org/draft/2020-12/schema)), and lua-schema's docs say `$dynamicRef` "is not fully supported, especially for extendible schema" (`docs/index.md`). That is exactly the feature a strict 2020-12 metaschema would lean on. In draft-07, `$defs` is not a keyword, so qwe schemas use `definitions`. A `$ref` to `#/$defs/x` still resolves as a plain JSON Pointer, but the strict metaschema below would reject a `$defs` key unless we add it. Moving to 2020-12 later remains possible, because lua-schema supports it.
- **What we must supply.** These are its dependencies and the glue it needs:
  - **LPeg 1.1.0** (C Lua module, MIT; `lpeg.html` §License). It is required at load time by `src/schema/format.lua:14`. It builds with the upstream `-std=c99` flag (`makefile:28`) and compiled cleanly for me against LuaJIT headers apart from a harmless `luaL_newlib` redefinition warning. `re.lua` ships with it.
  - **A `compat53` shim.** On Lua < 5.3, `keyword.lua:6-7` does `require'compat53'` and then uses `utf8.len` and `math.tointeger`. Either vendor lua-compat-5.3 (MIT, UNVERIFIED which parts are needed) or preload a 15-line shim, as in the test in §8.
  - **A regex engine for `pattern`.** Without `rex_pcre2`, any schema that uses `pattern` fails at compile time with `rex_pcre2 missing` (tested). See §1.4.
  - **Array/object predicates.** By default, a Lua table counts as both `array` and `object`, so `{a=1}` passes `"type":"array"` (tested). The fix is `schema.json.is_array` / `is_object`. That hook exists only after release 0.1.2 (CHANGES "0.x.y: handles custom json.is_array & json.is_object"; used in `keyword.lua:670-671`), **so pin a commit, not the 0.1.2 tag**.
- **JSON parser for schema files: dkjson 2.8** (pure Lua, MIT, from [dkolf.de](http://dkolf.de/dkjson-lua/dkjson-2.8.lua); header lines 20-37). It needs no build. Given `nullval`, it returns tables tagged with `{__jsontype='object'|'array'}` metatables (`dkjson.lua:517-523, 601`), which plug straight into the predicates above. Built-in plugin `schema.json` files get embedded as strings and parsed the same way. lua-cjson (C, MIT) would also work but adds C for no gain on a handful of small files. cJSON (C89, MIT, [v1.7.19](https://github.com/DaveGamble/cJSON/releases/tag/v1.7.19)) is only worth it if schemas must be parsed in C, and this design does not need that.

### 1.4 `pattern` (ECMA-262 regex)

Lua patterns are not regexes. lua-schema adds non-standard `luaPattern`/`lpegPattern` keywords (`docs/schema.md`), but the standard `pattern` needs PCRE2 through lrexlib (`keyword.lua:22-23, 894-906`). Two viable options:

1. **(Recommended when `pattern` is first needed.)** Vendor **PCRE2** 10.48, released 2026-08-31 ([release](https://github.com/PCRE2Project/pcre2/releases)). Its license is BSD-3-Clause WITH PCRE2-exception ([LICENCE.md](https://github.com/PCRE2Project/pcre2/blob/master/LICENCE.md)). Its README says it builds with "a C99 or later compiler". It **ships its own `BUILD.bazel`** that only *copies* the checked-in `config.h.generic`, `pcre2.h.generic` and `pcre2_chartables.c.dist` ([BUILD.bazel](https://github.com/PCRE2Project/pcre2/blob/master/BUILD.bazel)), so there is no configure step. Then register a `pattern` override through `schema.custom_keyword.pattern`, which takes priority over the built-in (`core.lua:229`). The override should call PCRE2 through a ~60-line C binding or through lrexlib's `rex_pcre2` (MIT, [LICENSE](https://github.com/rrthomas/lrexlib/blob/master/LICENSE), last commit 2026-08-20). PCRE2 is not ECMA-262 either, but it is much closer than POSIX.
2. **(Cheapest for M1.)** Leave `pattern` out of the strict metaschema until a plugin needs it, and offer `luaPattern` for simple cases. POSIX `regcomp(REG_EXTENDED)` from glibc is a tempting zero-vendoring route. WJElement and jsonc-daccord both use it (`schema.c:1116`, `regex_match.c`). But ERE has no `\d`, lookaround or lazy quantifiers, so it quietly accepts a different language. Avoid it, or reject non-ERE syntax.

### 1.5 CDDL

CDDL ([RFC 8610](https://www.rfc-editor.org/rfc/rfc8610)) is CBOR's own schema language. The only C tooling found (zcbor) turns CDDL into C with a Python generator (§3.1). No runtime C CDDL validator was found (UNVERIFIED that none exists). Editor and LSP support for workflow YAML is built around JSON Schema. Nothing here is decisive, so **stay with JSON Schema**.

---

## 2. Comparison tables

### 2.1 CBOR libraries in C

| Name | License (verified) | Last release / last commit | C std, C++-free? | Deps | Tags | Allocation | Bazel notes | Verdict |
|---|---|---|---|---|---|---|---|---|
| **TinyCBOR** [intel/tinycbor](https://github.com/intel/tinycbor) | MIT (`LICENSE`) | v7.0, 2026-02-18; `bac6648` 2026-09-14 | C99 (`CMakeLists.txt:85-90`); library is pure C; only tests are C++/Qt | libc, libm (float files) | Encode and decode any tag (`cbor_encode_tag`, `cbor_value_get_tag`) | None in core; caller buffer or writer callback. `cbor_value_dup_*` mallocs (optional file) | `cbor.h` includes `tinycbor-export.h` and `tinycbor-version.h`, which CMake generates. Hand-write them: copy `src/tinycbor-export.h.in` as-is (it only defines an empty `CBOR_API`), and `tinycbor-version.h` = the `.in` with MAJOR=7, MINOR=0. Sources: `cborencoder.c cborencoder_close_container_checked.c cborerrorstrings.c cborparser.c cborvalidation.c cborencoder_float.c cborparser_float.c`; add `-lm`. Built with `-std=c99 -Wall -Wextra -Werror` with no warnings. `-pedantic` warns only about the compiler-gated `_Float16` and `__attribute__((fallthrough))`, which are harmless | **Chosen** |
| QCBOR [laurencelundblade/QCBOR](https://github.com/laurencelundblade/QCBOR) | BSD-3-Clause (`LICENSE`) | v1.6.1, 2026-03-20; v2.0-alpha-6 2026-05-01; `65fd7cb` 2026-09-04 | "Dependent only on C99" (README); compiled cleanly with `-std=c99 -pedantic` | libc (+ optional libm) | Yes; v1 maps up to `QCBOR_MAX_TAGS_PER_ITEM` (4) tags per item; README says v2 has "better tag handling" | None ("Malloc is not needed", README) | Plain sources in `src/`, headers in `inc/`, no generated headers; easiest to build | Strong runner-up. Rejected: nesting capped at `QCBOR_MAX_ARRAY_NESTING 15` (`qcbor_common.h:587`); the encoder back-patches container heads on close (`qcbor_encode.c:891, 942`), which shifts byte offsets; the stateful decode context is harder to rewind; v2 still alpha |
| libcbor [PJK/libcbor](https://github.com/PJK/libcbor) | MIT (`LICENSE.md`) | v0.14.0, 2026-07-18; `5ab2b26` 2026-09-17 | C99 by default, but CMake switches to C23 when `[[nodiscard]]` compiles (`CMakeLists.txt:66-78`); compiled fine with `-std=c99` | libc | Yes (`cbor_build_tag`, `cbor_tag_value`) | Heap tree of ref-counted `cbor_item_t`; malloc per item (`cbor_set_allocs`) | The upstream `BUILD` runs **cmake inside a genrule** and `cc_import`s the `.a`, so it is not a plain `cc_library`. Doable by hand-writing `cbor/configuration.h` (7 macros) and `cbor/cbor_export.h`; I compiled all 20 `.c` files that way | Rejected: malloc-per-node tree is heavier than needed; upstream Bazel path is CMake |
| NanoCBOR [bergzand/NanoCBOR](https://github.com/bergzand/NanoCBOR) | CC0-1.0 (`LICENSE`) | no tags/releases; `a9eaf98` 2026-09-10 | Meson `c_std=gnu99`; under `-std=c99` it warns on anonymous unions and implicit `htobe64`/`be64toh` (needs `_DEFAULT_SOURCE`) | libc | `nanocbor_get_tag`/`get_tag64`, `nanocbor_fmt_tag` | None | Two `.c` files, `include/nanocbor/config.h` | Rejected: gnu99, no releases to pin, built for known structures on MCUs |
| cn-cbor [cabo/cn-cbor](https://github.com/cabo/cn-cbor), [jimsch/cn-cbor](https://github.com/jimsch/cn-cbor) | MIT (`LICENSE`) | cabo `c2e5373` 2020-04-08; jimsch `f713bf6` 2020-08-21, tag 1.0.0 | C | libc | `CN_CBOR_TAG` | Node tree, calloc (or context allocator) | CMake | Rejected: unmaintained since 2020; the author's README says "you are most likely better off using" the jimsch fork, which is also dormant |
| zcbor [zephyrproject-rtos/zcbor](https://github.com/zephyrproject-rtos/zcbor) | Apache-2.0 (`LICENSE`) | tag 0.9.1; `9164bd1` 2026-01-14 | C ("C++ compatible") | Python for codegen/validation | yes (`zcbor_tags.h`) | None | — | Excluded: its schema story is **CDDL compiled into C by a Python script** (README "Code generation"). The bare C library can be used without generated code (README "CBOR decoding/encoding library"), but it offers nothing over TinyCBOR and pulls towards codegen |

### 2.2 CBOR on the Lua side

| Name | Lang | License (verified) | Last release / commit | Deps | Text vs bytes | Tags | Verdict |
|---|---|---|---|---|---|---|---|
| **Own C module over TinyCBOR** | C99 | ours | — | TinyCBOR, Lua C API | we decide (e.g. valid UTF-8 → text, `cbor.bytes(s)` wrapper → bytes) | we decide (secret → distinct userdata/metatable) | **Chosen** |
| lua-ConciseSerialization [framagit fperrad](https://framagit.org/fperrad/lua-ConciseSerialization) | pure Lua | MIT (`COPYRIGHT`) | 0.2.4 (CHANGES: 2023-11-20); `a16c5dd` 2026-01-03 | none (uses `jit` if present) | strings → **text** by default (`CBOR.lua:879`); decoded byte and text strings both become Lua strings | `coders.tag` + `register_tag`; **unregistered tags silently dropped** (tested) | Pure-Lua fallback only |
| lua-cbor (Zash / Kim Alvefur) [code.zash.se](https://code.zash.se/lua-cbor/), [luarocks](https://luarocks.org/modules/zash/lua-cbor) | "mostly pure" Lua | MIT (`COPYING` in rock `lua-cbor-1.0.0-1.src.rock`, hg node `a1b820511c92`) | 1.0.0 (rock dated 2022-08-27); web browsing of the repo is disabled ("no longer available due to overly aggressive scraping") | optional `struct`, `bit` | **every Lua string → byte string (major type 2)**, incl. map keys: `encoder.string = encoder.bytestring` (`cbor.lua:~205`); `{a=1}` → `a1 41 61 01` (tested) | `cbor.tagged(tag, v)` round-trips (tested) | Rejected |
| org.conman.cbor [spc476/CBOR](https://github.com/spc476/CBOR) | Lua + C module (`cbor_c.c`) | **LGPL-3.0+** (`LICENSE`; rockspec `license = "LGPL3+"`) | 1.4.0, 2025-01-31 | **LPeg**, its own C module `org.conman.cbor_c` | — | full | **Disqualified (copyleft)** |
| LuaJIT FFI over TinyCBOR | Lua | — | — | symbols exported from the binary (`-rdynamic`/export list) | — | — | Rejected: 59 `static inline` APIs are not FFI-callable; struct layouts duplicated in `ffi.cdef` |

### 2.3 JSON Schema validators

| Name | Lang | License (verified) | Last release / commit | Drafts | `const` / `oneOf` / `$ref` / defs / `additionalProperties` / `pattern` | Error location | Metaschema check | Deps | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| **lua-schema** [framagit fperrad](https://framagit.org/fperrad/lua-schema) | pure Lua (5.1–5.5) | MIT (`LICENSE`) | 0.1.2 (CHANGES: 2026-08-05); `1a14a04` 2026-09-18 | 4, 6, 7, 2019-09, 2020-12, v1-2026 (`docs/index.md`); `$dynamicRef` partial | all yes; `pattern` needs `rex_pcre2` or a `custom_keyword` override | JSON Pointer `instanceLocation` + `keywordLocation` (flag/basic/detailed output) | `schema.new` validates against the metaschema; metaschema objects exposed (`require'schema.draft-07'`) | LPeg (C, MIT), compat53 (shim), optional rex_pcre2 | **Chosen** |
| api7/jsonschema [api7/jsonschema](https://github.com/api7/jsonschema) | Lua (generates Lua source, `loadstring`) | Apache-2.0 (`LICENSE`) | v0.9.13, 2026-04-29 | 4, 6, 7 (README) | `const` yes (`jsonschema.lua:1159`); `oneOf` yes; pattern via `ngx.re` or **lrexlib-pcre** (rockspec `lrexlib-pcre = 2.9.1-1`), overridable | **prose string only**: `"property jobs validation failed: …"`; `oneOf` failure is just "matches none" (`:1103`) | none built in | net-url, lrexlib-pcre (PCRE1), optional cjson; reads the global `ngx` at load (`if ngx then`, `:43`), which would trip strict globals | Rejected: no structured instance path, no metaschema, PCRE1 |
| lua-resty-ljsonschema [Tieske](https://github.com/Tieske/lua-resty-ljsonschema) | Lua (codegen via `load`) | MIT (`LICENSE.md`) | 1.2.0 (CHANGELOG: 2024-10-23); `704c08a` 2025-11-25 | **draft 4 only** (README) | **no `const`** (no match for `schema.const` in `init.lua`) | prose strings | ships a draft-4 metaschema module (`metaschema.lua`) that requires cjson (`:184`) | net-url, cjson | Rejected: draft 4, no `const` |
| ljsonschema [jdesgats](https://github.com/jdesgats/ljsonschema) | Lua (codegen) | MIT (`LICENSE`) | no tags; `48909cc` 2019-06-15 | draft 4 (README) | no `const` | prose strings | no | net-url | Rejected: abandoned, draft 4 |
| WJElement [netmail-open/wjelement](https://github.com/netmail-open/wjelement) | C | README offers GPL / LGPL / MIT, but `src/wjelement/schema.c` header says LGPL only; json-schema.org lists it as **AGPL-3.0** and "obsolete" ([tooling-data.yaml](https://github.com/json-schema-org/website/blob/main/data/tooling-data.yaml)) | v1.3, 2017-06-13; `c5495a2` 2026-03-23 | draft 3; "Draft 4 is barely supported" (`schema.c:426`) | no `const` | callback text | no | own JSON lib, POSIX regex, CMake `configure_file` | Rejected: drafts, conflicting license |
| jsonc-daccord [getCUJO/jsonc-daccord](https://github.com/getCUJO/jsonc-daccord) | C | MIT (`LICENSE`) | tag `libjsoncdac-0.5-20240902`; `9a06419` 2026-03-03 | not stated; keyword list incl. `const`, `$defs`, `$ref` | yes, but pattern = POSIX ERE (`regex_match.c`) | schema-keyword tree, not instance pointer (`output.c`); optional build flag | none | **json-c** (CMake-generated `json_config.h`), `config.h.in`, optional curl | Rejected: data must be json-c objects, no metaschema, no instance paths |
| jsonschema-c [helmut-jacob/jsonschema-c](https://github.com/helmut-jacob/jsonschema-c) | C | MIT (`COPYING`) | `4abad5a` 2016-01-19 | draft 4 | no `const` | — | draft-4 schema validator | json-c, autotools | Rejected: abandoned, draft 4 |
| valijson, pboettch/json-schema-validator, jsoncons, hotkit/json-schema, Blaze | C++ | — | — | — | — | — | — | — | **Excluded (C++)**, confirmed by `languages: ['C++']` in json-schema.org's `tooling-data.yaml` |

---

## 3. Why each rejected candidate lost

### 3.1 CBOR

- **QCBOR.** High quality: BSD-3, strict C99, no malloc, active. It loses for three concrete reasons.
  - Nesting is capped at 15 by a compile-time constant (`qcbor_common.h:587`, "Do not increase this over 255"). A workflow already uses about 6 levels before plugin `with:` data starts.
  - The encoder writes container heads when the container is closed and "slides data to the right" (`qcbor_encode.c:942`). Any side table keyed by byte offset, which is one way to do ADR-0008 positions, would be invalidated while encoding.
  - The decoder is a 312-byte stateful context rather than a copyable cursor, which makes backtracking in a validator or converter clumsier.

  If TinyCBOR ever disappoints, QCBOR is the drop-in alternative.
- **libcbor.** MIT and active, but it builds a malloc'd, ref-counted tree per item. Its own Bazel support shells out to CMake in a genrule (`BUILD`). Hand-writing its two generated headers does work (verified), but we gain nothing over TinyCBOR's zero-allocation cursor.
- **NanoCBOR.** CC0 and tiny, but it needs GNU C (`c_std=gnu99` in `meson.build`, anonymous unions, `htobe64`). It has no tagged releases to pin, and it is tuned for decoding *known* structures on microcontrollers.
- **cn-cbor.** Both repos have been dormant since 2020, and the original author points users to the fork, which is itself dormant.
- **zcbor.** Its validation and schema story is Python codegen from CDDL, which §2.4 of the design forbids. The standalone C part is redundant with TinyCBOR.
- **lua-cbor (Zash).** It encodes every Lua string as a byte string, and on LuaJIT there is no `utf8.len` to guess otherwise, so every map key crosses to C as a byte string. It also has fallback float-decoding bugs on LuaJIT: `read_double` tests `exponent ~= 0xff` where it should test `0x7ff`, so `2^-768` decodes as `inf` (tested). Its source is only reachable by `hg` or a rock download.
- **org.conman.cbor.** LGPL-3.0+, so it is disqualified. It also needs LPeg plus its own C module.
- **lua-ConciseSerialization.** A good pure-Lua library. It is not needed because the C module wins, and it silently drops unknown tags. Keep it as the fallback.

### 3.2 JSON Schema

- **api7/jsonschema.** Supports drafts 4/6/7 with `const`, and it is actively released. But errors are a single prose string with the path buried in the text. A `oneOf` miss reports only "matches none", so the kernel cannot turn it into `file:line:col`. It needs PCRE1 through lrexlib-pcre and net-url. It checks the `ngx` global at load time, which conflicts with strict globals. It has no metaschema check.
- **lua-resty-ljsonschema, ljsonschema.** Draft 4 only, with no `const`. The ticket needs `const` for the discriminated `with:` unions. Errors are prose strings. The original is abandoned (2019).
- **WJElement.** Draft 3 with partial draft 4 and no `const`. The license is contradictory: the README says tri-license including MIT, the source headers say LGPL, and json-schema.org lists AGPL-3.0. Last release 2017.
- **jsonc-daccord.** The best pure-C option found, but not good enough:
  - it requires json-c objects, so CBOR → json-c conversion plus json-c's CMake-generated headers,
  - it has no metaschema validation,
  - its error output is a schema-keyword tree rather than an instance pointer,
  - its `pattern` is POSIX ERE.
- **jsonschema-c.** Draft 4, abandoned in 2016, autotools, json-c.
- **Writing a C validator ourselves.** Not needed. The spec (`.scratch/m1-engine/spec.md:199`) explicitly allows validating in Lua. lua-schema passes the ticket's two validator tests on LuaJIT today (§8).

---

## 4. Catching `"requried"` concretely

Standard metaschemas do **not** reject unknown keywords. The draft-07 metaschema root has `properties` for the known keywords but **no `additionalProperties`** (`draft07.json` from [json-schema.org/draft-07/schema](https://json-schema.org/draft-07/schema); every `additionalProperties` in it sits under `definitions`/`properties`/`patternProperties`/`dependencies`). So `{"type":"object","requried":["x"]}` is *valid* against it. Tested: `require'schema.draft-07':validate{type="object", requried={"x"}}` → `valid = true`, and `schema.new` accepts it too.

What catches it is a **qwe strict metaschema**, built like this:
1. Take the official draft-07 metaschema JSON unchanged. Vendor it as `schemas/meta/draft-07.json`, or reuse lua-schema's table.
2. Change `$id` to a qwe URI, for example `https://qwe.invalid/meta/strict-draft-07`.
3. Add `"additionalProperties": false` at the root.
4. Optionally add keys qwe itself defines, such as `"secret": {"type":"boolean"}` for output schemas. Optionally remove keywords qwe doesn't support, such as `pattern` until §1.4 is done, or `format`. Optionally pin `"$schema": {"const": "http://json-schema.org/draft-07/schema#"}` so plugin authors can't switch drafts.

Because every subschema slot in draft-07 points back with `{"$ref":"#"}`, the rule applies at every depth. `properties.x.minLenght` is caught as well. A *property named* `requried` inside `properties: {...}` is still allowed, as it should be.

Result, tested on LuaJIT with lua-schema at `1a14a04`:

```
{type=object, requried=[x]}                     -> valid=false  | /requried  /additionalProperties
{properties={x={type=string, minLenght=1}}}     -> valid=false  | /properties/x/minLenght  /additionalProperties
{required=[x], properties={x={oneOf=[{const=a},{const=b}]}}} -> valid=true
```

So `qwe validate` runs `strict:validate(plugin_schema)` and then `schema.new(plugin_schema)`. The instance pointer (`/requried`) maps back to the position in `schema.json`. That only works if schema files also carry positions, which is an open question in §6.

A 2020-12 version of this needs either a flattened single-file strict metaschema that we maintain ourselves, or `unevaluatedProperties:false` with `$dynamicAnchor: meta`. lua-schema warns that the latter is shaky. That is why draft-07 is recommended.

---

## 5. Data path and the glue we must write

Chosen path: **validate in Lua, on the parent's LuaJIT state, over Lua tables converted from CBOR in C.**

```
workflow.yaml ──libyaml──▶ transcoder (C) ──TinyCBOR──▶ CBOR bytes  +  position table
                                                          │              (JSON Pointer → line:col,
                                                          │               for keys AND values)
                              qwe.cbor C module: CBOR ──▶ Lua tables (ARR/OBJ metatables,
                                                          │               null sentinel, secret type)
schema.json ──dkjson──▶ Lua tables ──▶ strict metaschema check ──▶ schema.new()
                                                          │
                                        validator:validate(tables) ──▶ errors[{instanceLocation="/jobs/build/steps/1/with/pth", ...}]
                                                          │
                                  C: look up pointer in position table ──▶ file:line:col
```

Glue to write, with rough sizes:

1. **`qwe.cbor` C module (TinyCBOR ↔ Lua)**, about 300–400 lines.
   - `decode` maps are set up as follows:
     - maps get an `OBJ` metatable and arrays an `ARR` metatable (these feed lua-schema's `is_object`/`is_array` and fix the empty-`{}` ambiguity),
     - `null` becomes a sentinel shared with `schema.json.null`,
     - text and byte strings become Lua strings, with byte strings optionally wrapped,
     - the secret tag becomes a distinct type,
     - there is a depth limit.
   - `encode` goes the other way. Plugin children use it to send the result and outputs on the result pipe. The parent uses it to receive them.
   - Integer caveat: LuaJIT numbers are doubles, so integers beyond 2^53 lose precision. Reject them or carry them as strings.
2. **Position table keyed by JSON Pointer.** The transcoder already has to build it (ADR-0008, ticket 04). Keying it by pointer, for example `/jobs/build/steps/1/with`, makes the lookup trivial. Record the **key** position as well as the value position: `additionalProperties` errors point at the unknown key (`/steps/1/with/pth`), and ticket 05 wants the key's position. An alternative is to key by CBOR byte offset and walk the CBOR by pointer at error time. That works with TinyCBOR because offsets are stable with indefinite-length containers.
3. **lua-schema bootstrap**, about 40 lines of Lua:
   - a `compat53` preload (`utf8.len`, `math.tointeger`),
   - `schema.json.null/is_array/is_object`,
   - `require'schema.draft-07'` with `schema.default_schema = "http://json-schema.org/draft-07/schema"` (the library's default is `https://json-schema.org/v1`, tested),
   - `schema.output_format = 'basic'`,
   - an optional `custom_keyword.pattern`.

   It must run in an environment exempt from strict globals, or with `utf8` pre-declared, because `keyword.lua:20` reads the global `utf8`.
4. **Error selection for `oneOf` unions.** lua-schema reports *every* branch's errors under `oneOf` (tested: branch 0's real error `/steps/1/with/pth` is mixed with branch 1's `const` failure on `/steps/1/uses`). Two fixes:
   - (a) compose the step union as `allOf: [ {if: {properties: {uses: {const: "file.ensure"}}, required: ["uses"]}, then: {$ref: "#/definitions/file.ensure"}}, ... ]` plus `uses: {enum: [...]}`. Errors then come only from the matching branch. This is cleaner, and the kernel composes this schema itself (§6.4).
   - (b) keep `oneOf` and drop the branches whose discriminator `const` failed. Prefer (a).
5. **Embedding.** Put lua-schema, dkjson and LPeg's `re.lua` into the binary as bytecode, the same way built-in plugins are. LPeg's `.c` files go into a `cc_library` linked into the binary and registered with `package.preload`.

**Why not validate in C?** There is no usable C99 validator (§3.2). Writing one means reimplementing `$ref` resolution, `oneOf` backtracking, output formats and the metaschema check, likely 2–3k lines. The Lua conversion step (item 1) is needed regardless, because plugins receive `with:` as Lua tables. The only cost of the Lua route is converting the workflow to tables once, at load time, which is negligible at workflow sizes.

---

## 6. Risks and open questions

1. **lua-schema is young.** It says "beta stage" (`docs/index.md`), its first release was 2025-12-23 (CHANGES), and it has one maintainer. The features we need are pinned to an **unreleased** commit (`is_array`/`is_object`). Mitigations:
   - vendor at `1a14a04c8586ce136d39f8e189f032a460c81ef1`,
   - run the upstream JSON-Schema-Test-Suite for draft-07 in a Bazel test,
   - keep our tests on the features we actually use.

   Its Bowtie compliance report ([bowtie.report](https://bowtie.report/#/implementations/lua-schema)) is JS-rendered and I could not read the numbers: **UNVERIFIED**.
2. **Secrets inside validated data.** After conversion, a `!encrypted` value is a secret-typed Lua value, not a string, so `with: {password: {type: string}}` would reject it. Qwe needs a convention for this. Options:
   - a custom keyword such as `"secret": "allowed"`, registered through `schema.custom_keyword`,
   - a custom `type`,
   - substituting a placeholder string before validation.

   This decision belongs with ticket 15.
3. **Pick the secret tag number.** RFC 8949 §9.2 reserves 32768 and above for First Come First Served ([RFC 8949 §9.2](https://www.rfc-editor.org/rfc/rfc8949.html#section-9.2)). Choose one in that range, optionally register it, and put it in one header shared by the C and Lua sides.
4. **Positions for schema files.** Metaschema errors on a plugin's `schema.json` (`/requried`) need a JSON line:col. dkjson doesn't give positions. Options:
   - accept `schema.json` + pointer in the message,
   - transcode `schema.json` through the same YAML→CBOR edge, since JSON is almost entirely valid YAML 1.1/1.2 and libyaml would supply positions (UNVERIFIED for every edge case, e.g. duplicate keys),
   - a small position-aware JSON scanner.
5. **`pattern` dialect.** Neither PCRE2 nor POSIX is ECMA-262. Document the dialect qwe accepts, or reject `pattern` in the strict metaschema until it is needed.
6. **Empty containers and `null` inside Lua.** The ARR/OBJ metatables handle `[]` vs `{}`. Lua sequences can't hold `nil`, so the null sentinel must be used consistently by `qwe.cbor`, dkjson (`nullval`) and lua-schema (`json.null`).
7. **TinyCBOR build details.**
   - `cborparser_dup_string.c` and `cborpretty_stdio.c` are optional.
   - `cbortojson.c` could produce `result.json` from CBOR, but it relies on `open_memstream`/`fopencookie` detection (CMake `check_symbol_exists`). It is simpler to write `result.json` from Lua with dkjson or from our own code.
   - `main` is 6 commits ahead of v7.0 as of 2026-09-19 (GitHub compare API). Pin the tag.
8. **LPeg version.** lua-schema uses `lpeg.utfR` (`format.lua:26`), which exists from LPeg 1.1 on. Pin 1.1.0.
9. **Strict globals.** api7 (rejected) reads `ngx`. lua-schema reads `utf8`. dkjson's global use under a strict `_G` was not audited: **UNVERIFIED**. Load third-party Lua with its own environment.
10. **Should this be an ADR?** Vendored libraries are reversible, but "validation runs in Lua on the parent's state, over tables converted from CBOR" shapes the kernel API (§4.3 of the design). An ADR is warranted.

---

## 7. Proposed pins (for `## Answer` in the ticket)

| Component | Version / pin | License |
|---|---|---|
| TinyCBOR | tag `v7.0` (tag ref `6442e749ca81`) | MIT |
| lua-schema | commit `1a14a04c8586ce136d39f8e189f032a460c81ef1` (framagit) | MIT |
| LPeg | 1.1.0 tarball (`https://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz`) | MIT |
| dkjson | 2.8 (`http://dkolf.de/dkjson-lua/dkjson-2.8.lua`) | MIT |
| compat53 | local shim (no vendoring) | — |
| PCRE2 (only when `pattern` is enabled) | 10.48 | BSD-3-Clause WITH PCRE2-exception |

Spike locations for the acceptance criteria: `third_party/tinycbor/roundtrip_test.c::tagged_value_survives`. The validator tests `::metaschema_rejects_typo` and `::oneof_const_discriminator` would sit next to the schema bootstrap, e.g. `src/kernel/schema/` (Lua test driven by a `cc_test` or the plugin test runner).

---

## 8. Spot checks performed (evidence)

All of these were run under `/home/user/.claude/jobs/bfa23448/tmp/research/`.

- **TinyCBOR** (`build_tinycbor/rt.c`). Compiled the 7 core `.c` files with `gcc -std=c99 -Wall -Wextra -Werror` using hand-written `tinycbor-export.h` and `tinycbor-version.h`. The test encodes an indefinite-length map `{ "secret": <tag 1886680369>(h'010203'), "arr": [-5, true, null] }`, runs `cbor_value_validate_basic`, re-reads it, and checks that the tag number and the byte-string type survive. Output: `n=26 tag=1886680369 isbytes=1`, exit 0.
- **QCBOR**: compiled all `src/*.c` with `-std=c99 -pedantic`, 0 warnings. **libcbor**: compiled all 20 `.c` files with `-std=c99` using hand-written `cbor/configuration.h` and `cbor/cbor_export.h`. **NanoCBOR**: `-std=c99` warns (anonymous union, implicit `htobe64`).
- **lua-schema on LuaJIT** (`lstest/t.lua`, `lstest/strict.lua`, `lstest/arr.lua`):
  - the plain draft-07 metaschema accepts `requried`,
  - the strict metaschema rejects it at `/requried`,
  - a `oneOf` + `const` step union reports `/steps/1/with/pth` (`additionalProperties`) and `/steps/1/with` (`required`),
  - a default `{a=1}` passes `type: array`, and custom predicates fix it,
  - `pattern` without PCRE2 raises `rex_pcre2 missing`.
- **Lua CBOR libraries** (`lstest/z.lua`, `lstest/f.lua`):
  - Zash encodes `"hi"` as `42 68 69` (a byte string) and `{a=1}` as `a1 41 61 01`, and decodes `2^-768` as `inf`,
  - fperrad encodes `"hi"` as `62 68 69` (a text string), round-trips a tagged secret object through `register_tag` + a `tocbor` hook, and drops an unregistered tag (`d8 20 61 78` → `"x"`).
