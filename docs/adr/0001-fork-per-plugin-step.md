# Every step plugin runs in a forked child

The kernel design doc (§5.3) had plugins run synchronously on the main thread. That means a Lua step that blocks (a tight loop, or a blocking FFI `read()`) freezes the epoll loop: no timeout can fire, Ctrl-C does nothing, and no other job's log is drained. LuaJIT debug hooks can't reliably interrupt JIT-compiled traces or blocking C calls. So the kernel forks for every step plugin. The child already has the loaded LuaJIT state, runs the step, writes log bytes to stdout and stderr, and returns its result and outputs to the parent as CBOR over a separate pipe. After the fork, `uses:` and `run:` steps are the same thing to the kernel: a process group with pipes, torn down by SIGTERM, then a grace period, then SIGKILL. Plugin load, schema registration and service plugins still run in-process.

## Considered options

- **Cooperative coroutines with a yielding kernel API.** Rejected. One plugin that blocks outside that API freezes the whole engine, and plugin-author discipline is the only safeguard.
- **Plugins as "glue only", with all real work in subprocesses.** Rejected. It's a rule the kernel can't enforce.

## Consequences

- Step plugins can't change kernel state. Everything they hand back goes over the result pipe (see ADR-0004).
- Inside the child, the plugin API can use plain blocking calls, because blocking the child is harmless.
- A plugin that segfaults, leaks memory or hangs affects only its own step.
