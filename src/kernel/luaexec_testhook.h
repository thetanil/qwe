/* Test-only fault injection for qwe.exec.run (ticket sca-round3/02).
 *
 * The two Lua API calls that hand exec_run's captured stdout/stderr to Lua
 * (see luaexec.c) run after the child is forked, its descriptors closed and
 * it is reaped -- so by that point a raise there must leak neither a
 * descriptor nor a zombie, only the (already-owned) output buffer, which
 * luaown.h covers. There is no cheap way to force a genuine Lua allocation
 * failure at exactly that point (LuaJIT's default build allocates its heap
 * through its own mmap arena, not libc malloc, so src/kernel/oom_shim.c's
 * --wrap=malloc cannot reach it -- see third_party/luajit/BUILD). This hook
 * is the fallback the ticket allows instead: arm it, make the call, and it
 * raises exactly where a real one would have. */
#ifndef QWE_KERNEL_LUAEXEC_TESTHOOK_H
#define QWE_KERNEL_LUAEXEC_TESTHOOK_H

/* which: 1 raises in place of the stdout push, 2 in place of the stderr
 * push, 0 disarms. Never set outside a test. */
void qwe_exec_run_test_force_raise(int which);

#endif
