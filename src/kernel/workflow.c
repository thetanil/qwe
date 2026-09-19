#define _GNU_SOURCE
#include "src/kernel/qwe.h"

#include "src/edge/yaml/transcode.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"
#include "src/kernel/validate.h"
#include "src/kernel/proc.h"
#include "src/kernel/result.h"
#include "src/kernel/ring.h"
#include "src/kernel/sink.h"

#include <errno.h>
#include <lauxlib.h>
#include <lualib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHUNK 65536

/* What the forked child needs to work out its argv. */
struct child_arg {
	lua_State *L;
	int step_ref; /* registry ref of the step's table */
};

/* Runs in the child: asks the step's plugin for the argv to exec. */
static char **child_argv(void *arg)
{
	struct child_arg *a = arg;
	lua_State *L = a->L;
	char **argv;
	size_t n, i;

	lua_getglobal(L, "require");
	lua_pushstring(L, "run");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	lua_getfield(L, -1, "argv");
	lua_rawgeti(L, LUA_REGISTRYINDEX, a->step_ref);
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	if (!lua_istable(L, -1)) {
		fprintf(stderr, "qwe: plugin run returned no argv\n");
		return NULL;
	}
	n = lua_objlen(L, -1);
	argv = calloc(n + 1, sizeof *argv);
	if (!argv)
		return NULL;
	for (i = 0; i < n; i++) {
		lua_rawgeti(L, -1, (int)i + 1);
		argv[i] = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	return argv;
fail:
	fprintf(stderr, "qwe: plugin run: %s\n", lua_tostring(L, -1));
	return NULL;
}

static int read_file(const char *path, char **out, size_t *len)
{
	FILE *fp = fopen(path, "rb");
	size_t cap = 4096, n = 0, got;
	char *buf;

	if (!fp)
		return -1;
	buf = malloc(cap);
	while (buf && (got = fread(buf + n, 1, cap - n, fp)) > 0) {
		n += got;
		if (n == cap) {
			char *grown = realloc(buf, cap *= 2);
			if (!grown) {
				free(buf);
				buf = NULL;
			} else {
				buf = grown;
			}
		}
	}
	fclose(fp);
	if (!buf)
		return -1;
	*out = buf;
	*len = n;
	return 0;
}

static int mkdir_p(const char *path)
{
	char *p = strdup(path), *s;
	int rc = 0;

	for (s = p + 1; rc == 0 && *s; s++) {
		if (*s != '/')
			continue;
		*s = '\0';
		if (mkdir(p, 0755) < 0 && errno != EEXIST)
			rc = -1;
		*s = '/';
	}
	if (rc == 0 && mkdir(p, 0755) < 0 && errno != EEXIST)
		rc = -1;
	free(p);
	return rc;
}

/* Feeds whatever is readable on fd through the ring into the sink.
 * Returns 1 at end of file, 0 if it would block. */
static int drain(int fd, struct qwe_ring *ring, struct qwe_sink *sink)
{
	char buf[CHUNK];

	for (;;) {
		ssize_t n = read(fd, buf, sizeof buf);
		unsigned long before = ring->dropped;
		char out[CHUNK];
		size_t got;

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return 0; /* EAGAIN */
		if (n == 0)
			return 1;
		qwe_ring_write(ring, buf, (size_t)n);
		while ((got = qwe_ring_read(ring, out, sizeof out)) > 0)
			qwe_sink_write(sink, out, got);
		if (ring->dropped != before) {
			char alarm[96];
			snprintf(alarm, sizeof alarm, "qwe: ALARM: ring overflow, %lu bytes dropped\n", ring->dropped);
			qwe_sink_write(sink, alarm, strlen(alarm));
		}
	}
}

/* Runs one step to completion. Returns the process's wait status, or -1. */
static int run_step(lua_State *L, int step_ref, sigset_t *chld, struct qwe_ring *ring, struct qwe_sink *sink)
{
	struct child_arg arg = {L, step_ref};
	struct qwe_proc proc;
	struct epoll_event ev;
	int sfd, ep, status = -1, exited = 0;

	sfd = signalfd(-1, chld, SFD_CLOEXEC | SFD_NONBLOCK);
	ep = epoll_create1(EPOLL_CLOEXEC);
	if (sfd < 0 || ep < 0)
		return -1;
	if (qwe_proc_spawn(&proc, child_argv, &arg) < 0)
		return -1;

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);
	ev.data.fd = proc.out_fd;
	epoll_ctl(ep, EPOLL_CTL_ADD, proc.out_fd, &ev);

	while (!exited) {
		struct epoll_event got[2];
		int i, n = epoll_wait(ep, got, 2, -1);

		if (n < 0 && errno == EINTR)
			continue;
		for (i = 0; i < n; i++) {
			if (got[i].data.fd == proc.out_fd) {
				if (drain(proc.out_fd, ring, sink))
					epoll_ctl(ep, EPOLL_CTL_DEL, proc.out_fd, NULL);
			} else {
				struct signalfd_siginfo si;
				while (read(sfd, &si, sizeof si) == (ssize_t)sizeof si)
					;
				if (waitpid(proc.pid, &status, WNOHANG) == proc.pid)
					exited = 1;
			}
		}
	}
	/* The child is gone; whatever it wrote is already in the pipe. */
	drain(proc.out_fd, ring, sink);
	close(proc.out_fd);
	close(sfd);
	close(ep);
	return status;
}

static void fmt_run_id(char *buf, size_t n)
{
	time_t now = time(NULL);
	struct tm tm;

	gmtime_r(&now, &tm);
	strftime(buf, n, "%Y%m%dT%H%M%SZ", &tm);
	snprintf(buf + strlen(buf), n - strlen(buf), "-%d", (int)getpid());
}

/* Reads, transcodes, decodes and validates the workflow. On success returns 0
 * with the decoded workflow on top of *L's stack. Otherwise it has printed
 * every error (as file:line:col) and returns QWE_EXIT_USAGE, and *L is closed. */
static int load_workflow(const char *cmd, const char *path, lua_State **L_out)
{
	char err[256], *yaml = NULL;
	size_t yaml_len, cbor_len;
	uint8_t *cbor;
	struct qwe_positions *pos = NULL;
	lua_State *L;
	int errors;

	if (read_file(path, &yaml, &yaml_len) < 0) {
		fprintf(stderr, "%s: cannot read %s: %s\n", cmd, path, strerror(errno));
		return QWE_EXIT_USAGE;
	}
	if (qwe_yaml_to_cbor(yaml, yaml_len, &cbor, &cbor_len, &pos, err, sizeof err) < 0) {
		fprintf(stderr, "%s: %s:%s\n", cmd, path, err);
		free(yaml);
		return QWE_EXIT_USAGE;
	}
	free(yaml);

	L = qwe_lua_new();
	if (!L) {
		fprintf(stderr, "%s: cannot start the Lua runtime\n", cmd);
		return QWE_EXIT_USAGE;
	}
	if (qwe_cbor_to_lua(L, cbor, cbor_len, err, sizeof err) < 0) {
		fprintf(stderr, "%s: %s: %s\n", cmd, path, err);
		lua_close(L);
		free(cbor);
		qwe_positions_free(pos);
		return QWE_EXIT_USAGE;
	}
	free(cbor);
	errors = qwe_validate_doc(L, cmd, path, pos);
	qwe_positions_free(pos);
	if (errors > 0) {
		lua_close(L);
		return QWE_EXIT_USAGE;
	}
	*L_out = L;
	return 0;
}

/* Leaves the workflow's only job table on top of the stack and stores its id
 * (malloc'd) in *job_id, or returns an error message. The tracer accepts
 * exactly one job. */
static const char *pick_job(lua_State *L, char **job_id)
{
	lua_getfield(L, -1, "jobs");
	if (!lua_istable(L, -1))
		return "workflow has no jobs";
	lua_pushnil(L);
	if (!lua_next(L, -2))
		return "workflow has no jobs";
	if (!lua_isstring(L, -2) || !lua_istable(L, -1))
		return "job is not a map";
	*job_id = strdup(lua_tostring(L, -2));
	lua_pop(L, 1); /* the value; the key stays for lua_next */
	if (lua_next(L, -2))
		return "more than one job is not implemented yet";
	lua_getfield(L, -1, *job_id); /* stack: workflow, jobs, job */
	return NULL;
}

int qwe_run_workflow(const char *path)
{
	char run_id[64], *dir, *run_dir;
	lua_State *L;
	char *job_id = NULL;
	const char *msg = NULL, *slash;
	struct qwe_ring ring;
	struct qwe_sink sink;
	struct qwe_step_result step;
	struct qwe_job_result job;
	sigset_t chld;
	int status, step_ref, rc = QWE_EXIT_OK;
	FILE *fp;

	if (load_workflow("qwe run", path, &L) != 0)
		return QWE_EXIT_USAGE;

	/* Tracer scope: one job, target local, one `run:` step. */
	if (!lua_istable(L, -1) || (msg = pick_job(L, &job_id)) != NULL) {
		fprintf(stderr, "qwe run: %s: %s\n", path, msg ? msg : "workflow is not a map");
		return QWE_EXIT_USAGE;
	}
	lua_getfield(L, -1, "target");
	if (!lua_isstring(L, -1) || strcmp(lua_tostring(L, -1), "local") != 0) {
		fprintf(stderr, "qwe run: %s: job %s: only target: local is implemented\n", path, job_id);
		return QWE_EXIT_USAGE;
	}
	lua_pop(L, 1);
	lua_getfield(L, -1, "steps");
	if (!lua_istable(L, -1) || lua_objlen(L, -1) != 1) {
		fprintf(stderr, "qwe run: %s: job %s: exactly one step is implemented\n", path, job_id);
		return QWE_EXIT_USAGE;
	}
	lua_rawgeti(L, -1, 1);
	lua_getfield(L, -1, "run");
	if (!lua_isstring(L, -1)) {
		fprintf(stderr, "qwe run: %s: job %s: step has no run:\n", path, job_id);
		return QWE_EXIT_USAGE;
	}
	lua_pop(L, 1);
	lua_getfield(L, -1, "id");
	step.id = lua_isstring(L, -1) ? strdup(lua_tostring(L, -1)) : NULL;
	lua_pop(L, 1);
	step_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the step table */

	/* .qwe/runs/<run-id>/ next to the workflow. */
	slash = strrchr(path, '/');
	dir = slash ? strndup(path, (size_t)(slash - path)) : strdup(".");
	fmt_run_id(run_id, sizeof run_id);
	run_dir = malloc(strlen(dir) + strlen(run_id) + 16);
	sprintf(run_dir, "%s/.qwe/runs/%s", dir, run_id);
	if (mkdir_p(run_dir) < 0) {
		fprintf(stderr, "qwe run: cannot create %s: %s\n", run_dir, strerror(errno));
		return QWE_EXIT_USAGE;
	}

	if (qwe_ring_init(&ring, QWE_RING_CAPACITY) < 0 || qwe_sink_open(&sink, job_id, run_dir, 1) < 0) {
		fprintf(stderr, "qwe run: cannot open the job log in %s: %s\n", run_dir, strerror(errno));
		return QWE_EXIT_USAGE;
	}

	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, NULL);

	job.started = step.started = time(NULL);
	status = run_step(L, step_ref, &chld, &ring, &sink);
	job.ended = step.ended = time(NULL);

	step.changed = 1; /* run: steps always count as changed */
	if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		step.outcome = "success";
		step.reason = NULL;
	} else {
		step.outcome = "failed";
		step.reason = "exit-code";
		rc = QWE_EXIT_FAILED;
	}
	job.id = job_id;
	job.outcome = step.outcome;
	job.reason = step.reason;
	job.dropped_bytes = ring.dropped;
	job.steps = &step;
	job.nsteps = 1;

	qwe_sink_close(&sink);
	qwe_ring_free(&ring);

	strcat(run_dir, "/result.json");
	fp = fopen(run_dir, "w");
	if (!fp || qwe_result_write(fp, run_id, &job, 1) < 0 || fclose(fp) != 0) {
		fprintf(stderr, "qwe run: cannot write %s: %s\n", run_dir, strerror(errno));
		rc = QWE_EXIT_FAILED;
	}
	lua_close(L);
	free(run_dir);
	free(dir);
	return rc;
}

int qwe_validate_workflow(const char *path)
{
	lua_State *L;
	int rc = load_workflow("qwe validate", path, &L);

	if (rc == 0)
		lua_close(L);
	return rc == 0 ? QWE_EXIT_OK : rc;
}
