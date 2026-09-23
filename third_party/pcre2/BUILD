load("@rules_cc//cc:defs.bzl", "cc_library")

# PCRE2 10.44 (PCRE2Project/pcre2), BSD. 8-bit library only, JIT and DFA tools
# excluded, no filesystem access at build or run time.
#
# config.h, pcre2.h and pcre2_chartables.c are the upstream *.generic / *.dist
# files, copied unchanged: PCRE2's own documented way to build without
# autotools or CMake (see src/config.h.generic's header comment). The boolean
# HAVE_*/SUPPORT_* macros they leave unset are supplied on the command line
# below instead of by editing the files.
#
# pcre2_jit_compile.c is always part of upstream's source list (even without
# JIT support it compiles to a small stub that reports "not supported"), so
# it is built too; the sljit backend it would otherwise pull in is gated by
# `#ifdef SUPPORT_JIT`, which is never defined here. pcre2_jit_match.c,
# pcre2_jit_misc.c and pcre2_ucptables.c are `#include`d by, respectively,
# pcre2_jit_compile.c and pcre2_tables.c rather than compiled on their own,
# so they are listed as hdrs, not srcs.
cc_library(
    name = "pcre2",
    srcs = [
        "src/pcre2_auto_possess.c",
        "src/pcre2_chartables.c",
        "src/pcre2_chkdint.c",
        "src/pcre2_compile.c",
        "src/pcre2_config.c",
        "src/pcre2_context.c",
        "src/pcre2_convert.c",
        "src/pcre2_dfa_match.c",
        "src/pcre2_error.c",
        "src/pcre2_extuni.c",
        "src/pcre2_find_bracket.c",
        "src/pcre2_jit_compile.c",
        "src/pcre2_maketables.c",
        "src/pcre2_match.c",
        "src/pcre2_match_data.c",
        "src/pcre2_newline.c",
        "src/pcre2_ord2utf.c",
        "src/pcre2_pattern_info.c",
        "src/pcre2_script_run.c",
        "src/pcre2_serialize.c",
        "src/pcre2_string_utils.c",
        "src/pcre2_study.c",
        "src/pcre2_substitute.c",
        "src/pcre2_substring.c",
        "src/pcre2_tables.c",
        "src/pcre2_ucd.c",
        "src/pcre2_valid_utf.c",
        "src/pcre2_xclass.c",
    ],
    hdrs = [
        "src/config.h",
        "src/pcre2.h",
        "src/pcre2_internal.h",
        "src/pcre2_intmodedep.h",
        "src/pcre2_jit_match.c",
        "src/pcre2_jit_misc.c",
        "src/pcre2_ucp.h",
        "src/pcre2_ucptables.c",
    ],
    copts = [
        "-w",
        "-std=gnu99",
        "-DHAVE_CONFIG_H",
        "-DHAVE_MEMMOVE",
        "-DHAVE_STRERROR",
        "-DHAVE_STDINT_H",
        "-DHAVE_INTTYPES_H",
        "-DHAVE_STDLIB_H",
        "-DHAVE_STRING_H",
        "-DHAVE_UNISTD_H",
        "-DSUPPORT_PCRE2_8",
        "-DSUPPORT_UNICODE",
    ],
    includes = ["src"],
    # PCRE2_CODE_UNIT_WIDTH selects the 8/16/32-bit flavour of every function
    # and type pcre2.h declares; any consumer including pcre2.h needs it too,
    # so it travels as a public define rather than a private one.
    defines = ["PCRE2_CODE_UNIT_WIDTH=8"],
    visibility = ["//visibility:public"],
)
