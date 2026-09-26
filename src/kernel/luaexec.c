#define _GNU_SOURCE
#include "src/kernel/luaexec.h"
#include "src/kernel/clock.h"
#include "src/kernel/gcov.h"
#include "src/kernel/preamble.h"
#include "src/kernel/errstr.h"

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
	if (b->len + n > b->cap) {
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

static void close_fd(int *fd)
{
	if (*fd >= 0)
		close(*fd);
	*fd = -1;
}

static int exec_run(lua_State *L)
{
	size_t n, i, in_len = 0, in_off = 0;
	const char *in = NULL;
	char **argv;
	int in_p[2] = {-1, -1}, out_p[2] = {-1, -1}, err_p[2] = {-1, -1};
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
	argv = calloc(n + 1, sizeof *argv);
	if (!argv)
		return luaL_error(L, "qwe.exec.run: out of memory");
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 1, (int)i + 1);
		argv[i] = strdup(luaL_checkstring(L, -1));
		lua_pop(L, 1);
		if (!argv[i]) {
			while (i-- > 0)
				free(argv[i]);
			free(argv);
			return luaL_error(L, "qwe.exec.run: out of memory");
		}
	}

	if (pipe2(in_p, O_CLOEXEC) < 0 || pipe2(out_p, O_CLOEXEC) < 0 || pipe2(err_p, O_CLOEXEC) < 0) {
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
		goto spawn_failed;
	}
	if (pid == 0) {
		sigset_t none;

		/* the parent blocks SIGCHLD, SIGINT and SIGTERM to read them from signalfds */
		sigemptyset(&none);
		pthread_sigmask(SIG_SETMASK, &none, NULL);
		sigaction(SIGPIPE, &old_pipe, NULL);
		dup2(in_p[0], 0);
		dup2(out_p[1], 1);
		dup2(err_p[1], 2);
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
	close_fd(&in_p[0]);
	close_fd(&out_p[1]);
	close_fd(&err_p[1]);
	if (in_len == 0)
		close_fd(&in_p[1]);
	if (background) {
		/* fire and forget: the caller never waits, the parent's child reaper collects it */
		close_fd(&in_p[1]);
		close_fd(&out_p[0]);
		close_fd(&err_p[0]);
		sigaction(SIGPIPE, &old_pipe, NULL);
		for (i = 0; i < n; i++)
			free(argv[i]);
		free(argv);
		lua_pushinteger(L, (lua_Integer)pid);
		return 1;
	}
	t0 = qwe_mono_now();
	fcntl(in_p[1], F_SETFL, O_NONBLOCK);

	while (out_p[0] >= 0 || err_p[0] >= 0 || in_p[1] >= 0) {
		struct pollfd pf[3];
		char chunk[16384];
		int np = 0, k;
		int which[3], w = -1;

		if (in_p[1] >= 0) {
			pf[np].fd = in_p[1];
			pf[np].events = POLLOUT;
			which[np++] = 0;
		}
		if (out_p[0] >= 0) {
			pf[np].fd = out_p[0];
			pf[np].events = POLLIN;
			which[np++] = 1;
		}
		if (err_p[0] >= 0) {
			pf[np].fd = err_p[0];
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
			int *fd = which[k] == 0 ? &in_p[1] : which[k] == 1 ? &out_p[0] : &err_p[0];
			ssize_t got;

			if (!pf[k].revents)
				continue;
			if (which[k] == 0) {
				got = write(*fd, in + in_off, in_len - in_off);
				if (got > 0)
					in_off += (size_t)got;
				if (got < 0 && errno != EAGAIN && errno != EINTR)
					close_fd(fd); /* the command stopped reading */
				if (in_off == in_len)
					close_fd(fd);
				continue;
			}
			got = read(*fd, chunk, sizeof chunk);
			if (got > 0) {
				if (buf_add(dst, chunk, (size_t)got) < 0)
					failed = 1;
			} else if (got == 0 || (errno != EAGAIN && errno != EINTR)) {
				close_fd(fd);
			}
		}
	}
	close_fd(&in_p[1]);
	close_fd(&out_p[0]);
	close_fd(&err_p[0]);
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
	for (i = 0; i < n; i++)
		free(argv[i]);
	free(argv);
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
	lua_pushlstring(L, out.data ? out.data : "", out.len);
	lua_pushlstring(L, err.data ? err.data : "", err.len);
	free(out.data);
	free(err.data);
	return 3;

spawn_failed:
	close_fd(&in_p[0]);
	close_fd(&in_p[1]);
	close_fd(&out_p[0]);
	close_fd(&out_p[1]);
	close_fd(&err_p[0]);
	close_fd(&err_p[1]);
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
	size_t n = 0, cap = 0, bad = 0, len;
	char *out;
	int rc;

	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushnil(L);
	while (lua_next(L, 1)) {
		if (n == cap) {
			const char **nn, **nv;

			cap = cap ? cap * 2 : 8;
			nn = realloc(names, cap * sizeof *names);
			if (nn)
				names = nn;
			nv = realloc(values, cap * sizeof *values);
			if (nv)
				values = nv;
			if (!nn || !nv) {
				free(names);
				free(values);
				return luaL_error(L, "qwe.exec.preamble: out of memory");
			}
		}
		/* the strings stay valid: the table holds them */
		names[n] = luaL_checkstring(L, -2);
		values[n] = luaL_checkstring(L, -1);
		n++;
		lua_pop(L, 1);
	}
	/* sorted by name, so the preamble does not depend on table order */
	{
		size_t i, j;

		for (i = 1; i < n; i++)
			for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
				const char *t = names[j];

				names[j] = names[j - 1];
				names[j - 1] = t;
				t = values[j];
				values[j] = values[j - 1];
				values[j - 1] = t;
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
	lua_pushlstring(L, out, len);
	free(out);
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
