load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//src:__subpackages__"])

# The kernel has no main. Only src/cli/main.c does.
cc_library(
    name = "kernel",
    srcs = [
        "kernel.c",
        "jobs.c",
        "validate.c",
        "workflow.c",
    ],
    hdrs = [
        "qwe.h",
        "jobs.h",
        "validate.h",
    ],
    deps = [
        ":alloc",
        ":clock",
        ":fmt",
        ":dag",
        ":lifecycle",
        ":redact",
        ":trace",
        ":luacbor",
        ":luavm",
        ":proc",
        ":timer",
        ":result",
        ":summary",
        ":ring",
        ":sched",
        ":sink",
        "//src/edge/yaml",
        "//third_party/luajit",
    ],
    visibility = ["//src:__subpackages__"],
)

cc_test(
    name = "api_test",
    size = "small",
    srcs = ["api_test.c"],
    deps = [
        ":kernel",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "ring",
    srcs = ["ring.c"],
    hdrs = ["ring.h"],
)

cc_test(
    name = "ring_test",
    size = "small",
    srcs = ["ring_test.c"],
    deps = [
        ":ring",
        "//third_party/greatest",
    ],
)

# qwe_gcov_dump: coverage of a process that ends in exec, _exit or abort (quality/09).
cc_library(
    name = "gcov",
    hdrs = ["gcov.h"],
)

cc_test(
    name = "gcov_test",
    size = "small",
    srcs = ["gcov_test.c"],
    deps = [
        ":gcov",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "proc",
    srcs = ["proc.c"],
    hdrs = ["proc.h"],
    deps = [":gcov"],
)

# Signals and process groups: its own test binary (spec, "Test strategy").
cc_test(
    name = "proc_test",
    size = "small",
    srcs = ["proc_test.c"],
    deps = [
        ":fmt",
        ":proc",
        ":luavm",
        "//third_party/luajit",
        ":timer",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "redact",
    srcs = ["redact.c"],
    hdrs = ["redact.h"],
    deps = [":alloc"],
)

cc_test(
    name = "redact_test",
    size = "small",
    srcs = ["redact_test.c"],
    deps = [
        ":redact",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "sink",
    srcs = ["sink.c"],
    hdrs = ["sink.h"],
    deps = [":alloc", ":fmt"],
)

cc_test(
    name = "sink_test",
    size = "small",
    srcs = ["sink_test.c"],
    deps = [
        ":fmt",
        ":sink",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "result",
    srcs = ["result.c"],
    hdrs = ["result.h"],
)

cc_test(
    name = "result_test",
    size = "small",
    srcs = ["result_test.c"],
    deps = [
        ":result",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "summary",
    srcs = ["summary.c"],
    hdrs = ["summary.h"],
    deps = [
        ":alloc",
        ":result",
    ],
)

cc_test(
    name = "summary_test",
    size = "small",
    srcs = ["summary_test.c"],
    deps = [
        ":fmt",
        ":summary",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "luacbor",
    srcs = ["luacbor.c"],
    hdrs = ["luacbor.h"],
    deps = [
        ":fmt",
        "//src/edge/yaml",
        "//third_party/luajit",
        "//third_party/tinycbor",
    ],
)

cc_library(
    name = "luavm",
    srcs = ["luavm.c", "luafs.c", "luaexec.c", "luasecrets.c"],
    hdrs = ["luavm.h", "luafs.h", "luaexec.h", "luasecrets.h"],
    # The Lua sources, so bazel coverage keeps their line coverage (see //tools/coverage:defs.bzl).
    data = [
        "//plugins/builtin:shipped",
        "//src/kernel/lua:shipped",
    ],
    deps = [
        ":gcov",
        ":clock",
        ":redact",
        "//src/secrets",
        "//third_party/libsodium",
        ":luacbor",
        ":preamble",
        "//src/kernel/lua:modules",
        "//third_party/lpeg",
        "//third_party/lrexlib:rex_pcre2",
        "//third_party/luajit",
    ],
)

cc_test(
    name = "luavm_test",
    size = "small",
    srcs = ["luavm_test.c"],
    deps = [
        ":luavm",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)

cc_library(
    name = "dag",
    srcs = ["dag.c"],
    hdrs = ["dag.h"],
    deps = [":fmt"],
)

cc_test(
    name = "dag_test",
    size = "small",
    srcs = ["dag_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    linkopts = ["-Wl,--wrap=calloc"],
    deps = [
        ":dag",
        "//third_party/greatest",
    ],
)

# Runs a Lua script inside qwe's Lua state. Used by tests.
cc_binary(
    name = "luarun",
    srcs = ["luarun.c"],
    visibility = ["//visibility:public"],
    deps = [
        ":luavm",
        "//third_party/luajit",
    ],
)

cc_test(
    name = "lua_cbor_test",
    size = "small",
    srcs = ["lua_cbor_test.c"],
    deps = [
        ":luacbor",
        ":luavm",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)

sh_test(
    name = "luarun_test",
    size = "small",
    srcs = ["luarun_test.sh"],
    args = ["$(location :luarun)"],
    data = [":luarun"],
)

# snprintf that says what a cut-short result means (fmt.h).
cc_library(
    name = "fmt",
    hdrs = ["fmt.h"],
)

cc_test(
    name = "fmt_test",
    size = "small",
    srcs = ["fmt_test.c"],
    deps = [
        ":fmt",
        "//third_party/greatest",
    ],
)

# CLOCK_MONOTONIC, aborting on the failure Linux cannot produce (clock.h).
cc_library(
    name = "clock",
    hdrs = ["clock.h"],
)

cc_test(
    name = "clock_test",
    size = "small",
    srcs = ["clock_test.c"],
    linkopts = ["-Wl,--wrap=clock_gettime"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":clock",
        ":gcov",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "timer",
    srcs = ["timer.c"],
    hdrs = ["timer.h"],
)

# Uses real timerfds and sleeps briefly, so it is its own test binary.
cc_test(
    name = "timer_test",
    size = "small",
    srcs = ["timer_test.c"],
    deps = [
        ":timer",
        "//third_party/greatest",
    ],
)

cc_library(
    name = "sched",
    srcs = ["sched.c"],
    hdrs = ["sched.h"],
    deps = [":lifecycle"],
)

cc_test(
    name = "sched_test",
    size = "small",
    srcs = ["sched_test.c"],
    deps = [
        ":lifecycle",
        ":sched",
        "//third_party/greatest",
    ],
)

# ADR-0010: the job lifecycle as one pure table.
cc_library(
    name = "lifecycle",
    srcs = ["lifecycle.c"],
    hdrs = ["lifecycle.h"],
)

# The event model is written apart from the table and only the test links it.
cc_test(
    name = "lifecycle_test",
    size = "small",
    srcs = [
        "lifecycle_model.c",
        "lifecycle_model.h",
        "lifecycle_test.c",
    ],
    deps = [
        ":fmt",
        ":lifecycle",
        ":trace",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

# The lifecycle trace file (design §7); linked into the kernel and the lifecycle test.
cc_library(
    name = "trace",
    srcs = ["trace.c"],
    hdrs = ["trace.h"],
    deps = [":clock", ":fmt", ":lifecycle"],
)

cc_test(
    name = "trace_test",
    size = "small",
    srcs = ["trace_test.c"],
    deps = [
        ":fmt",
        ":trace",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

# The stdin preamble that carries env to a command (design §12).
cc_library(
    name = "preamble",
    srcs = ["preamble.c"],
    hdrs = ["preamble.h"],
)

cc_test(
    name = "preamble_test",
    size = "small",
    srcs = ["preamble_test.c"],
    deps = [
        ":fmt",
        ":preamble",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

cc_test(
    name = "jobs_test",
    size = "small",
    srcs = ["jobs_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    linkopts = [
        "-Wl,--wrap=calloc",
        "-Wl,--wrap=strdup",
        "-Wl,--wrap=realloc",
        "-Wl,--wrap=free",
    ],
    deps = [
        ":kernel",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)

cc_library(
    name = "alloc",
    srcs = ["alloc.c"],
    hdrs = ["alloc.h"],
)

# The failing allocator (ticket quality/07). testonly: no shipping target can
# depend on it, so no binary that ships contains it. The linkopts travel with
# the library; a test using it must be linkstatic (--wrap only reaches objects
# linked into the binary).
cc_library(
    name = "oom_shim",
    testonly = True,
    srcs = ["oom_shim.c"],
    hdrs = ["oom_shim.h"],
    deps = [":gcov"],
    linkopts = [
        "-Wl,--wrap=malloc",
        "-Wl,--wrap=calloc",
        "-Wl,--wrap=realloc",
        "-Wl,--wrap=strdup",
        "-Wl,--wrap=strndup",
    ],
    visibility = ["//src:__subpackages__"],
    alwayslink = True,
)

cc_test(
    name = "alloc_test",
    size = "small",
    srcs = ["alloc_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":alloc",
        ":gcov",
        ":oom_shim",
        "//src/testing:owned",
        "//third_party/greatest",
    ],
)

cc_test(
    name = "oom_shim_test",
    size = "small",
    srcs = ["oom_shim_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":fmt",
        ":gcov",
        ":oom_shim",
        "//third_party/greatest",
    ],
)

# Fails every allocation on the load path in turn (ticket quality/02).
cc_test(
    name = "load_oom_test",
    size = "small",
    timeout = "eternal",  # minutes under valgrind: see .bazelrc, docs/valgrind.md
    srcs = ["load_oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    linkopts = [
        "-Wl,--wrap=malloc",
        "-Wl,--wrap=calloc",
        "-Wl,--wrap=realloc",
        "-Wl,--wrap=strdup",
        "-Wl,--wrap=strndup",
    ],
    deps = [
        ":fmt",
        ":kernel",
        "//third_party/greatest",
    ],
)

# Fails allocation in the forked step child (ticket quality/02).
cc_test(
    name = "workflow_test",
    size = "small",
    srcs = ["workflow_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    linkopts = ["-Wl,--wrap=calloc"],
    deps = [
        ":fmt",
        ":kernel",
        "//third_party/greatest",
    ],
)

# Each sanitizer config must be shown live: the child faults on purpose and
# the test passes only if the sanitizer stops it. Incompatible (skipped by
# //...) unless built with --config=ubsan or --config=asan.
cc_test(
    name = "sanitizer_smoke_ubsan_test",
    size = "small",
    srcs = ["sanitizer_smoke_test.c"],
    local_defines = ["QWE_SMOKE_UBSAN"],
    target_compatible_with = select({
        "//tools/sanitizers:ubsan": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

cc_test(
    name = "sanitizer_smoke_asan_test",
    size = "small",
    srcs = ["sanitizer_smoke_test.c"],
    local_defines = ["QWE_SMOKE_ASAN"],
    target_compatible_with = select({
        "//tools/sanitizers:asan": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

# trace.c as a libc without strerrorname_np builds it (musl, glibc < 2.32): the
# rename makes any call to the GNU function a compile error.
cc_test(
    name = "errno_name_test",
    size = "small",
    srcs = [
        "errno_name_test.c",
        "trace.c",
        "trace.h",
    ],
    local_defines = [
        "QWE_NO_STRERRORNAME_NP",
        "strerrorname_np=qwe_strerrorname_np_must_not_be_called",
    ],
    deps = [
        ":clock",
        ":fmt",
        ":lifecycle",
        "//third_party/greatest",
    ],
)

# Proves the valgrind gate catches an uninitialised read: the child's read must
# make valgrind fail it. Incompatible (skipped by //...) unless built with
# --config=valgrind.
cc_test(
    name = "valgrind_smoke_test",
    size = "small",
    srcs = ["valgrind_smoke_test.c"],
    # the uninitialised read is the point; the compiler would otherwise refuse it
    copts = ["-Wno-maybe-uninitialized"],
    target_compatible_with = select({
        "//tools/valgrind:valgrind": [],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)

# Fails every allocation of a qwe run in turn, in the engine and in the step child (ticket quality/07).
cc_test(
    name = "oom_test",
    size = "small",
    timeout = "eternal",  # minutes under valgrind: see .bazelrc, docs/valgrind.md
    srcs = ["oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":fmt",
        ":kernel",
        ":oom_shim",
        "//third_party/greatest",
    ],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)

# Fails when a bare allocation call appears (or goes) outside the audit list.
sh_test(
    name = "alloc_audit_test",
    size = "small",
    srcs = ["alloc_audit_test.sh"],
    args = ["$(rootpath alloc_audit.txt)"] + ["$(rootpaths %s)" % p for p in [
        ":c_files",
        "//src/secrets:c_files",
        "//src/edge/yaml:c_files",
        "//src/cli:c_files",
        "//src/cli/encrypt:c_files",
        "//src/cli/run:c_files",
        "//src/cli/validate:c_files",
        "//src/cli/keygen:c_files",
        "//src/cli/serve:c_files",
        "//tools:c_files",
    ]],
    data = [
        "alloc_audit.txt",
        ":c_files",
        "//src/cli:c_files",
        "//src/cli/encrypt:c_files",
        "//src/cli/keygen:c_files",
        "//src/cli/run:c_files",
        "//src/cli/serve:c_files",
        "//src/cli/validate:c_files",
        "//src/edge/yaml:c_files",
        "//src/secrets:c_files",
        "//tools:c_files",
    ],
)

# Fails every allocation of the redactor, the CBOR map encoder and qwe.exec.run in turn (ticket quality/08).
cc_test(
    name = "sites_oom_test",
    size = "small",
    srcs = ["sites_oom_test.c"],
    linkstatic = True,  # --wrap only reaches objects linked into the binary
    deps = [
        ":kernel",
        ":luavm",
        ":oom_shim",
        ":redact",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)

# qwe.exec with bad arguments and a starved system (ticket quality/09).
cc_test(
    name = "luaexec_test",
    size = "small",
    srcs = ["luaexec_test.c"],
    deps = [
        ":luavm",
        "//third_party/greatest",
        "//third_party/luajit",
    ],
)
