#define _GNU_SOURCE
#include "src/kernel/luaexec.h"
#include "src/kernel/preamble.h"

#include <errno.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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
	int detach = 0;

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
	}
	argv = calloc(n + 1, sizeof *argv);
	if (!argv)
		return luaL_error(L, "qwe.exec.run: out of memory");
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, 1, (int)i + 1);
		argv[i] = strdup(luaL_checkstring(L, -1));
		lua_pop(L, 1);
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
		sigprocmask(SIG_SETMASK, &none, NULL);
		sigaction(SIGPIPE, &old_pipe, NULL);
		dup2(in_p[0], 0);
		dup2(out_p[1], 1);
		dup2(err_p[1], 2);
		if (detach) {
			/* its own session, out of every step's group, and no output of its own */
			int nul = open("/dev/null", O_WRONLY);

			setsid();
			if (nul >= 0) {
				dup2(nul, 1);
				dup2(nul, 2);
			}
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	close_fd(&in_p[0]);
	close_fd(&out_p[1]);
	close_fd(&err_p[1]);
	if (in_len == 0)
		close_fd(&in_p[1]);
	fcntl(in_p[1], F_SETFL, O_NONBLOCK);

	while (out_p[0] >= 0 || err_p[0] >= 0 || in_p[1] >= 0) {
		struct pollfd pf[3];
		char chunk[16384];
		int np = 0, k;
		int which[3];

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
		if (poll(pf, (nfds_t)np, -1) < 0) {
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
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	sigaction(SIGPIPE, &old_pipe, NULL);
	for (i = 0; i < n; i++)
		free(argv[i]);
	free(argv);
	if (failed) {
		free(out.data);
		free(err.data);
		return luaL_error(L, "qwe.exec.run: out of memory");
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
	lua_pushstring(L, strerror(saved));
	return 2;
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
			cap = cap ? cap * 2 : 8;
			names = realloc(names, cap * sizeof *names);
			values = realloc(values, cap * sizeof *values);
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
	lua_createtable(L, 0, 5);
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
	return 1;
}
