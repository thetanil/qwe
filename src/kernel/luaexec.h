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
 * caller's process group, so a step's teardown reaches it, unless opts
 * (a third argument, a table) has detach = true: then the command gets a
 * session of its own and no stdout or stderr, as a daemon does (the ssh
 * ControlMaster, started by the parent).

 * Two more options bound or release the wait, for the parent's event loop, which
 * must not stall: timeout = <seconds> kills the command when it runs longer and
 * returns nil, "timed out after N ms"; background = true starts it (no output, no
 * wait) and returns its pid at once. qwe.exec.wait(pid, seconds) -> boolean waits
 * for such a command, up to the bound.
 * qwe.exec.getpid() and qwe.exec.getuid() are the process's ids. */
/* qwe.exec.preamble(env) -> the stdin preamble that carries a { NAME = "value" }
 * table (see preamble.h); qwe.exec.bootstrap is the shell script that reads it. */
int luaopen_qwe_exec(lua_State *L);

#endif
