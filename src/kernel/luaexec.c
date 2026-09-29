#define _GNU_SOURCE
#include "src/kernel/luaexec.h"
#include "src/kernel/clock.h"
#include "src/kernel/gcov.h"
#include "src/kernel/preamble.h"
#include "src/kernel/errstr.h"
#include "src/kernel/luaown.h"
#include "src/kernel/luaexec_testhook.h"

#include <errno.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct buf {
	char *data;
	size_t len, cap;
};

static int buf_add(struct buf *b, const char *src, size_t n)
{
	/* An empty buf (data NULL, cap 0) always grows first: said outright,
	 * since GCC 13's analyzer does not derive it from len + n > cap and
	 * reports a memcpy into NULL (ticket sca-round3/07). */
	if (!b->data || n > b->cap - b->len) {
		size_t cap = b->cap ? b->cap : 4096;
		char *grown;

		while (cap < b->len + n)
			cap *= 2;
		grown = realloc(b->data, cap);
		if (!grown)
			return -1;
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return 0;
}

/* The parent's end of a pipe to the command. Whether it is still open is a
 * flag of its own, not a -1 in fd: GCC 13's analyzer does not know that a
 * descriptor pipe2 returned is >= 0, so every "if (fd >= 0) close(fd)" was,
 * to it, a path that leaks (ticket sca-round3/07). */
struct end {
	int fd, open;
};

static void end_close(struct end *e)
{
	if (e->open)
		close(e->fd);
	e->open = 0;
}

/* The three pipes to a command, in one struct: as three separate array
 * parameters the analyzer must assume they may alias, and then a pipe2 into
 * one may overwrite -- leak -- the descriptors in another. */
struct pipes {
	int in[2], out[2], err[2];
};

static void close_pipe(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

/* All three pipes, or -1 with errno set and none of them open. */
static int open_pipes(struct pipes *p)
{
	int saved;

	if (pipe2(p->in, O_CLOEXEC) < 0)
		return -1;
	if (pipe2(p->out, O_CLOEXEC) < 0)
		goto close_in;
	if (pipe2(p->err, O_CLOEXEC) < 0)
		goto close_out;
	return 0;

close_out:
	saved = errno;
	close_pipe(p->out);
	errno = saved;
close_in:
	saved = errno;
	close_pipe(p->in);
	errno = saved;
	return -1;
}

static void close_pipes(struct pipes *p)
{
	close_pipe(p->in);
	close_pipe(p->out);
	close_pipe(p->err);
}

static int force_raise_at; /* see luaexec_testhook.h; 0 outside a test */

void qwe_exec_run_test_force_raise(int which)
{
	force_raise_at = which;
}

/* Raises in place of the which'th post-fork push, if a test armed it. */
static void maybe_force_raise(lua_State *L, int which)
{
	if (force_raise_at == which) {
		force_raise_at = 0;
		luaL_error(L, "qwe.exec.run: test-forced raise");
	}
}

static int exec_run(lua_State *L)
{
	size_t n, i, in_len = 0, in_off = 0;
	const char *in = NULL;
	char **argv;
	struct pipes pipes;
	struct end to_in, from_out, from_err;
	struct buf out = {0}, err = {0};
	struct sigaction ign, old_pipe;
	pid_t pid;
	int status = 0, failed = 0, saved = 0;
	int detach = 0, background = 0, timed_out = 0;
	long timeout_ms = -1;
	struct timespec t0;

	luaL_checktype(L, 1, LUA_TTABLE);
	n = lua_objlen(L, 1);
	if (n == 0)
		return luaL_error(L, "qwe.exec.run: empty argv");
	if (!lua_isnoneornil(L, 2))
		in = luaL_checklstring(L, 2, &in_len);
	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "detach");
		detach = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 3, "background");
		background = lua_toboolean(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, 3, "timeout");
		if (lua_isnumber(L, -1) && lua_tonumber(L, -1) > 0)
			timeout_ms = (long)(lua_tonumber(L, -1) * 1000.0);
		lua_pop(L, 1);
	}
	/* Validate every element is string-convertible first, with nothing yet
	 * on the C heap: a wrong-typed element raises here, before the first
	 * malloc, so nothing leaks (ticket sca-round3/02). Each luaL_checkstring
	 * is left on the stack (not popped) so the second pass can read it back
	 * with lua_tolstring, which -- given a value already a string -- cannot
	 * itself raise. */
	luaL_checkstack(L, (int)n + 8, "qwe.exec.run: too many arguments");
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 1, (int)i + 1);
		luaL_checkstring(L, -1);
	}
	argv = calloc(n + 1, sizeof *argv);
	if (!argv) {
		lua_pop(L, (int)n);
		return luaL_error(L, "qwe.exec.run: out of memory");
	}
	for (i = 0; i < n; i++) {
		argv[i] = strdup(lua_tostring(L, (int)i - (int)n));
		if (!argv[i]) {
			while (i-- > 0)
				free(argv[i]);
			free(argv);
			lua_pop(L, (int)n);
			return luaL_error(L, "qwe.exec.run: out of memory");
		}
	}
	lua_pop(L, (int)n);

	if (open_pipes(&pipes) < 0) {
		saved = errno;
		goto spawn_failed;
	}
	/* A command that exits without reading its stdin must not kill the step
	 * with SIGPIPE: the write just fails. */
	memset(&ign, 0, sizeof ign);
	ign.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &ign, &old_pipe);
	pid = fork();
	if (pid < 0) {
		saved = errno;
		sigaction(SIGPIPE, &old_pipe, NULL);
		close_pipes(&pipes);
		goto spawn_failed;
	}
	if (pid == 0) {
		sigset_t none;

		/* the parent blocks SIGCHLD, SIGINT and SIGTERM to read them from signalfds */
		sigemptyset(&none);
		pthread_sigmask(SIG_SETMASK, &none, NULL);
		sigaction(SIGPIPE, &old_pipe, NULL);
		dup2(pipes.in[0], 0);
		dup2(pipes.out[1], 1);
		dup2(pipes.err[1], 2);
		if (detach || background) {
			/* no output of its own; a detached command also gets a session of its own, out of every step's group */
			int nul = open("/dev/null", O_WRONLY);

			if (detach)
				setsid();
			if (nul >= 0) {
				dup2(nul, 1);
				dup2(nul, 2);
			}
		}
		qwe_gcov_dump();
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipes.in[0]);
	close(pipes.out[1]);
	close(pipes.err[1]);
	to_in.fd = pipes.in[1];
	from_out.fd = pipes.out[0];
	from_err.fd = pipes.err[0];
	to_in.open = from_out.open = from_err.open = 1;
	if (in_len == 0)
		end_close(&to_in);
	/* Nothing after the fork needs argv (the child already has its own copy
	 * across the fork): freeing it here, before any further Lua call, keeps
	 * it off the list of things a later raise could leak. */
	for (i = 0; i < n; i++)
		free(argv[i]);
	free(argv);
	if (background) {
		/* fire and forget: the caller never waits, the parent's child reaper collects it */
		end_close(&to_in);
		end_close(&from_out);
		end_close(&from_err);
		sigaction(SIGPIPE, &old_pipe, NULL);
		lua_pushinteger(L, (lua_Integer)pid);
		return 1;
	}
	t0 = qwe_mono_now();
	if (to_in.open)
		fcntl(to_in.fd, F_SETFL, O_NONBLOCK);

	while (from_out.open || from_err.open || to_in.open) {
		struct pollfd pf[3];
		char chunk[16384];
		int np = 0, k;
		int which[3], w = -1;

		if (to_in.open) {
			pf[np].fd = to_in.fd;
			pf[np].events = POLLOUT;
			which[np++] = 0;
		}
		if (from_out.open) {
			pf[np].fd = from_out.fd;
			pf[np].events = POLLIN;
			which[np++] = 1;
		}
		if (from_err.open) {
			pf[np].fd = from_err.fd;
			pf[np].events = POLLIN;
			which[np++] = 2;
		}
		if (timeout_ms >= 0) {
			struct timespec now;
			long left;

			now = qwe_mono_now();
			left = timeout_ms - ((long)(now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000);
			if (left <= 0) {
				timed_out = 1;
				break;
			}
			w = (int)left;
		}
		if (poll(pf, (nfds_t)np, w) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (k = 0; k < np; k++) {
			struct buf *dst = which[k] == 1 ? &out : &err;
			struct end *e = which[k] == 0 ? &to_in : which[k] == 1 ? &from_out : &from_err;
			ssize_t got;

			if (!pf[k].revents)
				continue;
			if (which[k] == 0) {
				got = write(e->fd, in + in_off, in_len - in_off);
				if (got > 0)
					in_off += (size_t)got;
				if (got < 0 && errno != EAGAIN && errno != EINTR)
					end_close(e); /* the command stopped reading */
				if (in_off == in_len)
					end_close(e);
				continue;
			}
			got = read(e->fd, chunk, sizeof chunk);
			if (got > 0) {
				if (buf_add(dst, chunk, (size_t)got) < 0)
					failed = 1;
			} else if (got == 0 || (errno != EAGAIN && errno != EINTR)) {
				end_close(e);
			}
		}
	}
	end_close(&to_in);
	end_close(&from_out);
	end_close(&from_err);
	/* Its output is closed, but it may still be running: the bound covers the wait too. */
	while (timeout_ms >= 0 && !timed_out) {
		struct timespec now;
		pid_t r = waitpid(pid, &status, WNOHANG);

		if (r != 0)
			goto reaped;
		now = qwe_mono_now();
		if ((long)(now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000 >= timeout_ms)
			timed_out = 1;
		else
			usleep(2000);
	}
	if (timed_out)
		kill(pid, SIGKILL);
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
reaped:
	sigaction(SIGPIPE, &old_pipe, NULL);
	if (failed) {
		free(out.data);
		free(err.data);
		return luaL_error(L, "qwe.exec.run: out of memory");
	}
	if (timed_out) {
		free(out.data);
		free(err.data);
		lua_pushnil(L);
		lua_pushfstring(L, "timed out after %d ms", (int)timeout_ms);
		return 2;
	}
	lua_pushinteger(L, WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
	/* out.data/err.data are heap buffers, and pushing each as a Lua string is
	 * a call that can itself raise on a Lua allocation failure. qwe_lua_own
	 * hands the buffer to Lua *before* that call, so the raise frees it
	 * instead of leaking it (ticket sca-round3/02); maybe_force_raise proves
	 * it, at exactly the point a real raise would land. */
	{
		int idx = qwe_lua_own(L, &out.data);

		maybe_force_raise(L, 1);
		qwe_lua_own_finish(L, idx, out.len);
	}
	{
		int idx = qwe_lua_own(L, &err.data);

		maybe_force_raise(L, 2);
		qwe_lua_own_finish(L, idx, err.len);
	}
	return 3;

spawn_failed: /* no pipe is open by here */
	for (i = 0; i < n; i++)
		free(argv[i]);
	free(argv);
	lua_pushnil(L);
	lua_pushstring(L, qwe_strerror(saved));
	return 2;
}

/* qwe.exec.wait(pid, seconds) -> true once the child (started with background = true)
 * is gone, false if it is still running after the bound. A child the event loop's
 * reaper collected first counts as gone. */
static int exec_wait(lua_State *L)
{
	pid_t pid = (pid_t)luaL_checkinteger(L, 1);
	long ms = (long)(luaL_checknumber(L, 2) * 1000.0), waited = 0;

	for (;;) {
		int status;
		pid_t r = waitpid(pid, &status, WNOHANG);

		if (r != 0) {
			lua_pushboolean(L, 1); /* exited, or not ours (any more) */
			return 1;
		}
		if (waited >= ms) {
			lua_pushboolean(L, 0);
			return 1;
		}
		usleep(2000);
		waited += 2;
	}
}

static int exec_getpid(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)getpid());
	return 1;
}

static int exec_getuid(lua_State *L)
{
	lua_pushinteger(L, (lua_Integer)getuid());
	return 1;
}

/* qwe.exec.preamble(env) -> the stdin preamble for a { NAME = "value" } table. */
static int exec_preamble(lua_State *L)
{
	const char **names = NULL, **values = NULL;
	size_t n = 0, bad = 0, len;
	char *out;
	int rc;

	luaL_checktype(L, 1, LUA_TTABLE);

	/* Pass 1, one traversal: every name and value must already be a string --
	 * checked by lua_type, never luaL_checkstring, since coercing a *key* in
	 * place confuses lua_next (Lua manual, lua_tolstring). Each key is
	 * copied into karr, a fresh Lua array, so pass 2 below can revisit them
	 * by index (a plain, bounded C loop, not a second opaque lua_next
	 * traversal the analyzer cannot see always matches the first one's
	 * count). Nothing is on the C heap yet, so a bad entry raises with
	 * nothing to leak (ticket sca-round3/02); building karr itself cannot
	 * leak either, being Lua's own memory. */
	lua_newtable(L);
	{
		int karr = lua_gettop(L);

		lua_pushnil(L);
		while (lua_next(L, 1)) {
			if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TSTRING)
				return luaL_error(L, "qwe.exec.preamble: env variable name and value must both be strings");
			lua_pushvalue(L, -2); /* ..., key, value, key_copy */
			lua_rawseti(L, karr, (int)++n); /* karr[n] = key_copy; pops it */
			lua_pop(L, 1); /* the value; the key stays for lua_next */
		}

		if (n) {
			size_t i;

			names = malloc(n * sizeof *names);
			values = malloc(n * sizeof *values);
			if (!names || !values) {
				free(names);
				free(values);
				return luaL_error(L, "qwe.exec.preamble: out of memory");
			}

			/* Pass 2: a plain, bounded loop -- karr has exactly n
			 * entries, and rawget is raw table access, like lua_next,
			 * so a metatable on the env table cannot re-enter here. */
			for (i = 0; i < n; i++) {
				lua_rawgeti(L, karr, (int)i + 1); /* key */
				lua_pushvalue(L, -1);
				lua_rawget(L, 1); /* ..., key, value */
				names[i] = lua_tostring(L, -2);
				values[i] = lua_tostring(L, -1);
				lua_pop(L, 2);
			}
			/* sorted by name, so the preamble does not depend on table order */
			for (i = 1; i < n; i++) {
				size_t j;

				for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
					const char *t = names[j];

					names[j] = names[j - 1];
					names[j - 1] = t;
					t = values[j];
					values[j] = values[j - 1];
					values[j - 1] = t;
				}
			}
		}
	}
	rc = qwe_preamble_build(names, values, n, &out, &len, &bad);
	if (rc < 0) {
		const char *name = n ? names[bad < n ? bad : 0] : "";

		free(names);
		free(values);
		return luaL_error(L, "env variable '%s': not a valid name", name);
	}
	free(names);
	free(values);
	/* out is a heap buffer, and pushing it as a Lua string is a call that can
	 * itself raise; own it first so that raise frees it (ticket sca-round3/02). */
	qwe_lua_own_pushlstring(L, &out, len);
	return 1;
}

int luaopen_qwe_exec(lua_State *L)
{
	lua_createtable(L, 0, 6);
	lua_pushcfunction(L, exec_getpid);
	lua_setfield(L, -2, "getpid");
	lua_pushcfunction(L, exec_getuid);
	lua_setfield(L, -2, "getuid");
	lua_pushcfunction(L, exec_preamble);
	lua_setfield(L, -2, "preamble");
	lua_pushstring(L, qwe_preamble_bootstrap);
	lua_setfield(L, -2, "bootstrap");
	lua_pushcfunction(L, exec_run);
	lua_setfield(L, -2, "run");
	lua_pushcfunction(L, exec_wait);
	lua_setfield(L, -2, "wait");
	return 1;
}
