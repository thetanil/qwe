load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("//tools/strip:defs.bzl", "stripped_binary")

cc_library(
    name = "dispatch",
    srcs = ["dispatch.c"],
    hdrs = ["dispatch.h"],
    deps = [
        "//src/kernel:put",
        "//src/cli/encrypt",
        "//src/cli/keygen",
        "//src/cli/run",
        "//src/cli/serve",
        "//src/cli/validate",
        "//src/kernel",
    ],
)



# The dynamic build is kept for the sanitizers and valgrind: a sanitizer runtime cannot be
# linked statically, and valgrind cannot intercept malloc in a static binary.
DYNAMIC = {
    "//tools/sanitizers:asan": True,
    "//tools/sanitizers:ubsan": True,
    "//tools/valgrind:valgrind": True,
    "//conditions:default": False,
}

# qwe-debug: the shipping binary with its symbols, statically linked.
cc_binary(
    name = "qwe-debug",
    srcs = ["main.c"],
    linkopts = select({
        "//tools/sanitizers:asan": [],
        "//tools/sanitizers:ubsan": [],
        "//tools/valgrind:valgrind": [],
        "//conditions:default": ["-static"],
    }),
    visibility = ["//visibility:public"],
    deps = [":dispatch"],
)

# qwe: the same, stripped. This is what ships and what the e2e cases run.
stripped_binary(
    name = "qwe",
    src = ":qwe-debug",
    keep_symbols = select(DYNAMIC),
    visibility = ["//visibility:public"],
)

# What "qwe has no threads" (ADR-0001) rests on, checked: the binary does not link
# pthread_create. The probe does, to show the check can tell.
cc_binary(
    name = "threads_probe",
    testonly = True,
    srcs = ["threads_probe.c"],
    linkopts = select({
        "//tools/sanitizers:asan": [],
        "//tools/sanitizers:ubsan": [],
        "//tools/valgrind:valgrind": [],
        "//conditions:default": ["-static"],
    }),
)

sh_test(
    name = "no_threads_test",
    size = "small",
    srcs = ["no_threads_test.sh"],
    args = [
        "$(location :qwe-debug)",
        "$(location :threads_probe)",
    ],
    data = [
        ":qwe-debug",
        ":threads_probe",
    ],
)

cc_test(
    name = "dispatch_test",
    size = "small",
    srcs = ["dispatch_test.c"],
    deps = [
        ":dispatch",
        "//third_party/greatest",
    ],
)

# The sources, for src/kernel:alloc_audit_test.
filegroup(
    name = "c_files",
    srcs = glob(["*.c"]),
    visibility = ["//src/kernel:__pkg__"],
)
