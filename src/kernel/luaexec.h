/* qwe.exec: runs one command and captures its output. It is how an execution
 * backend runs a command on the operator host. */
#ifndef QWE_KERNEL_LUAEXEC_H
#define QWE_KERNEL_LUAEXEC_H

#include <lua.h>

/* Opens the module: qwe.exec.run(argv, stdin) -> code, stdout, stderr.
 * argv is an array of strings; stdin is a string, or nil for /dev/null. code
 * is the exit status, or -N if the command was killed by signal N. It returns
 * nil, message if the command could not be started. Blocks until the command
 * exits, which is harmless in a step's forked child. The command joins the
 * caller's process group, so a step's teardown reaches it. */
int luaopen_qwe_exec(lua_State *L);

#endif
