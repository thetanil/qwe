load("@rules_cc//cc:defs.bzl", "cc_library")

# libsodium 1.0.20 (jedisct1/libsodium), ISC. src/ is the release tarball's
# src/libsodium, unchanged except that include/sodium/version.h is generated
# from version.h.in (what configure does). tarball sha256:
# ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19
#
# Only the portable C is built. The SIMD and assembly variants need CPU
# flags and configure's feature tests; with none of its HAVE_* macros set,
# libsodium picks its reference implementations, which is all secrets need.
cc_library(
    name = "libsodium",
    srcs = glob(
        [
            "src/**/*.c",
            "src/**/*.h",
        ],
        exclude = [
            "src/**/*_sse*",
            "src/**/*-sse*",
            "src/**/*avx*",
            "src/**/*armcrypto*",
            "src/**/*neon*",
            
            "src/**/*sandy2x*/**",
            "src/**/*xmm6*/**",
            "src/**/sse/**",
            "src/**/sse2/**",
            "src/**/dolbeau/**",
            "src/include/**",
        ],
    ),
    hdrs = glob(["src/include/**/*.h"]),
    copts = [
        "-DCONFIGURED=1",
        "-DDEV_MODE=0",
        "-DNATIVE_LITTLE_ENDIAN=1",
        "-D_GNU_SOURCE",
        "-DHAVE_ATOMIC_OPS=1",
        "-DHAVE_MMAP=1",
        "-DHAVE_MLOCK=1",
        "-DHAVE_MADVISE=1",
        "-DHAVE_MPROTECT=1",
        "-DHAVE_POSIX_MEMALIGN=1",
        "-DHAVE_GETRANDOM=1",
        "-DHAVE_SYS_RANDOM_H=1",
        "-DHAVE_SYS_MMAN_H=1",
        "-DHAVE_EXPLICIT_BZERO=1",
        "-DHAVE_SYSCONF=1",
        "-DASM_HIDE_SYMBOL=.hidden",
        "-Isrc/include/sodium",
        "-Ithird_party/libsodium/src/include/sodium",
        "-w",
    ],
    includes = ["src/include"],
    local_defines = ["SODIUM_STATIC", "SODIUM_EXPORT="],
    visibility = ["//visibility:public"],
)
