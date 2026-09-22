load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

# relver.txt is the commit time of that commit (git log -1 --format=%ct), which
# the Makefile would read from git. LuaJIT v2.1 rolling, commit c6ffc141a876 (2026-09-08), MIT. x86-64 Linux only.
# The Makefile's host-tool steps are replayed as genrules:
#   minilua -> genversion (luajit.h), dynasm (buildvm_arch.h)
#   buildvm -> lj_vm.S and the lj_*def.h headers

LIB_C = [
    "src/lib_base.c",
    "src/lib_math.c",
    "src/lib_bit.c",
    "src/lib_string.c",
    "src/lib_table.c",
    "src/lib_io.c",
    "src/lib_os.c",
    "src/lib_package.c",
    "src/lib_debug.c",
    "src/lib_jit.c",
    "src/lib_ffi.c",
    "src/lib_buffer.c",
]

cc_binary(
    name = "minilua",
    srcs = ["src/host/minilua.c"],
    # minilua is itself a small Lua interpreter (the same union-based tagged-value
    # representation as full LuaJIT) used only to generate other LuaJIT sources
    # (genversion.lua, dynasm.lua -> buildvm_arch.h): under --config=release, Bazel's
    # exec/host configuration builds tools like this one at -c opt regardless of the
    # target build's compilation mode, and -O2's strict-aliasing optimizer can silently
    # miscompile it. A code generator that runs once at build time gains nothing from
    # optimization and has everything to lose from running wrong, so pin it to -O0
    # -fno-strict-aliasing outright rather than relying on copt order to win the fight
    # with whatever -c opt the exec config is using.
    copts = ["-w", "-O0", "-fno-strict-aliasing"],
    linkopts = ["-lm"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "gen_luajit_h",
    srcs = [
        "src/host/genversion.lua",
        "src/luajit_rolling.h",
        "relver.txt",
    ],
    outs = ["src/luajit.h"],
    cmd = "$(location :minilua) $(location src/host/genversion.lua) " +
          "$(location src/luajit_rolling.h) $(location relver.txt) $@",
    tools = [":minilua"],
)

genrule(
    name = "gen_buildvm_arch_h",
    srcs = [
        "src/vm_x64.dasc",
        "dynasm/dynasm.lua",
        "dynasm/dasm_x86.lua",
        "dynasm/dasm_x64.lua",
    ],
    outs = ["src/host/buildvm_arch.h"],
    cmd = "$(location :minilua) $(location dynasm/dynasm.lua) " +
          "-D ENDIAN_LE -D P64 -D JIT -D FFI -D FPU -D HFABI -D SHADOW_STACK -D VER= " +
          "-o $@ $(location src/vm_x64.dasc)",
    tools = [":minilua"],
)

cc_binary(
    name = "buildvm",
    srcs = glob([
        "src/host/buildvm*.c",
        "src/host/buildvm*.h",
        "src/*.h",
        "dynasm/*.h",
    ]) + [
        "src/host/buildvm_arch.h",
        "src/luajit.h",
    ],
    copts = [
        "-w",
        # A code generator (produces lj_vm.S and the lj_*def.h headers by introspecting
        # LuaJIT's own struct layouts): same reasoning as minilua's copts above.
        "-O0",
        "-fno-strict-aliasing",
        "-DLUAJIT_TARGET=LUAJIT_ARCH_x64",
        "-DLJ_ARCH_HASFPU=1",
        "-DLJ_ABI_SOFTFP=0",
        "-DLJ_ABI_SHADOW_STACK=1",
    ],
    includes = [
        "src",
        "src/host",
        "dynasm",
    ],
)

genrule(
    name = "gen_lj_vm_s",
    outs = ["src/lj_vm.S"],
    cmd = "$(location :buildvm) -m elfasm -o $@",
    tools = [":buildvm"],
)

[genrule(
    name = "gen_" + mode,
    srcs = LIB_C,
    outs = ["src/lj_%sdef.h" % mode],
    cmd = "$(location :buildvm) -m %sdef -o $@ $(SRCS)" % mode,
    tools = [":buildvm"],
) for mode in [
    "bc",
    "ff",
    "lib",
    "rec",
]]

genrule(
    name = "gen_folddef",
    srcs = ["src/lj_opt_fold.c"],
    outs = ["src/lj_folddef.h"],
    cmd = "$(location :buildvm) -m folddef -o $@ $(SRCS)",
    tools = [":buildvm"],
)

cc_library(
    name = "luajit",
    srcs = glob(
        [
            "src/lj_*.c",
            "src/lib_*.c",
            "src/*.h",
        ],
        exclude = ["src/luajit.h"],
    ) + [
        "src/luajit.h",
        "src/lj_bcdef.h",
        "src/lj_ffdef.h",
        "src/lj_libdef.h",
        "src/lj_recdef.h",
        "src/lj_folddef.h",
        "src/lj_vm.S",
    ],
    hdrs = [
        "src/lauxlib.h",
        "src/lua.h",
        "src/lua.hpp",
        "src/luaconf.h",
        "src/luajit.h",
        "src/lualib.h",
    ],
    copts = [
        "-w",
        "-std=gnu99",
        "-fomit-frame-pointer",
        "-fno-stack-protector",
        # LuaJIT's tagged values (lj_obj.h's TValue) are type-punned through a union by
        # design; under -O2 (--config=release, ADR pending) GCC's strict-aliasing
        # optimizer miscompiles that into crashes (SIGSEGV, everywhere the VM runs) that
        # a fastbuild (-O0) or -c dbg (-O0) binary never shows. This flag is required for
        # any optimized LuaJIT build, on any platform, not specific to this repo.
        "-fno-strict-aliasing",
        "-U_FORTIFY_SOURCE",
        "-D_FILE_OFFSET_BITS=64",
        "-D_LARGEFILE_SOURCE",
        "-DLUAJIT_UNWIND_EXTERNAL",
    ],
    includes = ["src"],
    linkopts = [
        "-lm",
        "-ldl",
    ],
    visibility = ["//visibility:public"],
)
