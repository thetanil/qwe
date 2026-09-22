#define _GNU_SOURCE
#include "src/kernel/qwe.h"

#include "src/edge/yaml/transcode.h"
#include "src/kernel/alloc.h"
#include "src/kernel/jobs.h"
#include "src/kernel/lifecycle.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"
#include "src/kernel/validate.h"
#include "src/kernel/proc.h"
#include "src/kernel/redact.h"
#include "src/kernel/timer.h"
#include "src/kernel/trace.h"
#include "src/kernel/result.h"
#include "src/kernel/ring.h"
#include "src/kernel/sched.h"
#include "src/kernel/sink.h"
#include "src/kernel/summary.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio_ext.h>
#include <lauxlib.h>
#include <lualib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
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

/* Whether the step table at idx is a uses: step. */
static int step_uses_plugin(lua_State *L, int idx)
{
	int uses;

	lua_getfield(L, idx, "uses");
	uses = lua_isstring(L, -1);
	lua_pop(L, 1);
	return uses;
}

/* Writes the CBOR encoding of the Lua value at idx to the result pipe: the
 * step's result, apart from its stdout and stderr (ADR-0005). */
static int send_result(lua_State *L, int idx, int fd)
{
	uint8_t *buf;
	size_t len, off = 0;
	char err[128];

	if (qwe_lua_to_cbor(L, idx, &buf, &len, err, sizeof err) < 0) {
		fprintf(stderr, "qwe: cannot encode the step result: %s\n", err);
		return -1;
	}
	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			break;
		off += (size_t)n;
	}
	free(buf);
	return off == len ? 0 : -1;
}

/* Sends { status = status [, reason = reason] } over the result pipe. */
static int send_status(lua_State *L, int fd, const char *status, const char *reason)
{
	int rc;

	lua_newtable(L);
	lua_pushstring(L, status);
	lua_setfield(L, -2, "status");
	if (reason) {
		lua_pushstring(L, reason);
		lua_setfield(L, -2, "reason");
	}
	rc = send_result(L, lua_gettop(L), fd);
	lua_pop(L, 1);
	return rc;
}

/* Runs in the child: hands the step to its plugin (qwe.plugins.run_step). A
 * run: step or a run-like plugin gives back the argv to exec. A check/apply
 * plugin does its work here, sends its result to the parent and the child
 * exits. Whatever happens, the parent learns it from the result pipe (or from
 * its absence, if the child dies). */
static char **child_argv(void *arg, int result_fd)
{
	struct child_arg *a = arg;
	lua_State *L = a->L;
	char **argv;
	size_t n, i;
	int is_plugin;

	lua_rawgeti(L, LUA_REGISTRYINDEX, a->step_ref);
	is_plugin = step_uses_plugin(L, -1);
	lua_pop(L, 1);
	/* The parent's unwritten stdout is not this step's output. */
	__fpurge(stdout);
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.plugins");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	lua_getfield(L, -1, "run_step");
	lua_rawgeti(L, LUA_REGISTRYINDEX, a->step_ref);
	if (lua_pcall(L, 1, 2, 0) != 0)
		goto fail;
	/* stack: module, argv or nil, result or nil */
	if (lua_istable(L, -1)) {
		int ok;

		lua_getfield(L, -1, "status");
		ok = lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "ok") == 0;
		lua_pop(L, 1);
		/* a result that could not be sent is a step that failed, not one that succeeded */
		if (send_result(L, lua_gettop(L), result_fd) < 0) {
			send_status(L, result_fd, "failed", "engine-error");
			ok = 0;
		}
		fflush(stdout);
		qwe_lua_coverage_flush(L);
		_exit(ok ? 0 : 1);
	}
	if (!lua_istable(L, -2)) {
		fprintf(stderr, "qwe: the plugin returned no argv\n");
		return NULL;
	}
	/* Built before "exec" is sent: a failure here reports itself, and the parent
	 * never sees a step that started. */
	n = lua_objlen(L, -2);
	argv = calloc(n + 1, sizeof *argv);
	for (i = 0; argv && i < n; i++) {
		lua_rawgeti(L, -2, (int)i + 1);
		argv[i] = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
		if (!argv[i])
			break;
	}
	if (!argv || (n > 0 && !argv[n - 1])) {
		while (argv && i-- > 0)
			free(argv[i]);
		free(argv);
		fprintf(stderr, "qwe: out of memory building the step's command\n");
		send_status(L, result_fd, "failed", "engine-error");
		return NULL;
	}
	/* The parent starts the step's clock on "exec"; without it, the step has not started. */
	if (send_status(L, result_fd, "exec", NULL) < 0) {
		send_status(L, result_fd, "failed", "engine-error");
		return NULL;
	}
	qwe_lua_coverage_flush(L);
	if (lua_type(L, -1) == LUA_TSTRING) {
		size_t sl;
		const char *s = lua_tolstring(L, -1, &sl);
		int fd = memfd_create("qwe-stdin", 0);

		/* the bootstrap shell reads exactly the preamble; the rest is the command's */
		if (fd < 0 || write(fd, s, sl) != (ssize_t)sl || lseek(fd, 0, SEEK_SET) < 0 || dup2(fd, 0) < 0) {
			fprintf(stderr, "qwe: cannot set up the step's stdin: %s\n", strerror(errno));
			return NULL;
		}
		close(fd);
	}
	return argv;
fail:
	fprintf(stderr, "qwe: plugin failed: %s\n", lua_tostring(L, -1));
	if (is_plugin) {
		send_status(L, result_fd, "failed", "plugin-error");
		fflush(stdout);
		qwe_lua_coverage_flush(L);
		_exit(1);
	}
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
	if (!buf) {
		errno = ENOMEM;
		return -1;
	}
	*out = buf;
	*len = n;
	return 0;
}

/* Creates path and any missing parents (0755); path itself gets mode. */
static int mkdir_p(const char *path, mode_t mode)
{
	char *p = strdup(path), *s;
	int rc = 0;

	if (!p) {
		errno = ENOMEM;
		return -1;
	}

	for (s = p + 1; rc == 0 && *s; s++) {
		if (*s != '/')
			continue;
		*s = '\0';
		if (mkdir(p, 0755) < 0 && errno != EEXIST)
			rc = -1;
		*s = '/';
	}
	if (rc == 0 && mkdir(p, mode) < 0 && errno != EEXIST)
		rc = -1;
	free(p);
	return rc;
}

/* Creates the step's $QWE_OUTPUT file 0600 before the step exists, so a secret it
 * writes there is never readable by another user. The step appends to it. */
static int make_output_file(const char *path)
{
	int fd;

	if (!path)
		return 0;
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	return close(fd);
}

/* Feeds whatever is readable on fd through the ring into the sink.
 * Returns 1 at end of file, 0 if it would block. */
/* Puts bytes (already redacted) into the ring and on to the sink. */
static void emit(struct qwe_ring *ring, struct qwe_sink *sink, const char *data, size_t n)
{
	unsigned long before = ring->dropped;
	char out[CHUNK];
	size_t got;

	qwe_ring_write(ring, data, n);
	while ((got = qwe_ring_read(ring, out, sizeof out)) > 0)
		qwe_sink_write(sink, out, got);
	if (ring->dropped != before) {
		char alarm[96];
		snprintf(alarm, sizeof alarm, "qwe: ALARM: ring overflow, %lu bytes dropped\n", ring->dropped);
		qwe_sink_write(sink, alarm, strlen(alarm));
	}
}

/* Reads what the step wrote. Every byte passes through the redactor before it
 * reaches the ring, so a known secret is never in the log, on the terminal or
 * in any consumer (qwe-ssh-sec II.7). */
static int drain(int fd, struct qwe_ring *ring, struct qwe_sink *sink, struct qwe_redactor *red)
{
	char buf[CHUNK];

	for (;;) {
		ssize_t n = read(fd, buf, sizeof buf);
		struct qwe_redact_buf safe = {0};

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return 0; /* EAGAIN */
		if (n == 0)
			return 1;
		if (qwe_redact_feed(red, buf, (size_t)n, &safe) == 0 && safe.len > 0)
			emit(ring, sink, safe.data, safe.len);
		qwe_redact_buf_free(&safe);
	}
}

/* The step's output has ended: what the redactor held back was not a secret. */
static void drain_flush(struct qwe_ring *ring, struct qwe_sink *sink, struct qwe_redactor *red)
{
	struct qwe_redact_buf safe = {0};

	if (qwe_redact_flush(red, &safe) == 0 && safe.len > 0)
		emit(ring, sink, safe.data, safe.len);
	qwe_redact_buf_free(&safe);
}

/* The result pipe carries a few hundred bytes. A step that sends more than this
 * is broken, and its result is treated as missing. */
#define RESULT_MAX (1024 * 1024)

/* Reads whatever is on the step's result pipe into the job's buffer. Returns 1
 * at end of file, 0 if it would block. */
static int drain_result(struct job_run *r)
{
	char buf[4096];

	for (;;) {
		ssize_t n = read(r->proc.res_fd, buf, sizeof buf);

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0)
			return 0;
		if (n == 0)
			return 1;
		if (r->res_len + (size_t)n > RESULT_MAX) {
			r->res_overflow = 1;
			continue;
		}
		if (r->res_len + (size_t)n > r->res_cap) {
			size_t cap = r->res_cap ? r->res_cap * 2 : 1024;
			char *grown;

			while (cap < r->res_len + (size_t)n)
				cap *= 2;
			grown = realloc(r->res_buf, cap);
			if (!grown) {
				r->res_overflow = 1;
				continue;
			}
			r->res_buf = grown;
			r->res_cap = cap;
		}
		memcpy(r->res_buf + r->res_len, buf, (size_t)n);
		r->res_len += (size_t)n;
	}
}

/* What an epoll event is about. Every fd in the loop carries a tag: which job,
 * what kind, and the step it was opened for. The step is what lets a late
 * event from a step that has ended be told from an event of the live one. */
enum evkind { EV_OUT, EV_RESULT, EV_STEP_TIMER, EV_JOB_TIMER, EV_GRACE_TIMER, EV_CHILD, EV_CANCEL };

#define TAG_NO_JOB 0xFFFFFFFFu

static uint64_t tag_make(size_t job, enum evkind kind, long step)
{
	return ((uint64_t)job << 32) | ((uint64_t)kind << 24) | (uint64_t)((step + 1) & 0xFFFFFF);
}

/* What is shared by every job of a run. */
struct run_ctx {
	sigset_t chld_mask;
	int ep;               /* the one epoll every job's fds are on */
	int chld_fd;          /* signalfd for SIGCHLD */
	int cancel_fd;        /* signalfd for SIGINT and SIGTERM */
	int cancel_requested; /* sticky: the operator asked to stop */
	long grace_ms;        /* SIGTERM to SIGKILL */
	struct qwe_trace trace;
	struct job *jobs;
	size_t njobs;
	const char *run_dir;
	const char *run_id;
	char *run_dir_abs;    /* run_dir as an absolute path: $QWE_OUTPUT files go there */
	int wf_ref;           /* registry ref of the decoded workflow */
	lua_State *L;
};

/* Notes a pending SIGINT or SIGTERM. */
static int poll_cancel(struct run_ctx *ctx)
{
	struct signalfd_siginfo si;

	while (read(ctx->cancel_fd, &si, sizeof si) == (ssize_t)sizeof si)
		ctx->cancel_requested = 1;
	return ctx->cancel_requested;
}

static void fmt_run_id(char *buf, size_t n)
{
	time_t now = time(NULL);
	struct tm tm;

	gmtime_r(&now, &tm);
	strftime(buf, n, "%Y%m%dT%H%M%SZ", &tm);
	snprintf(buf + strlen(buf), n - strlen(buf), "-%d", (int)getpid());
}

/* Reads, transcodes, decodes and validates the inventory (inv_path, or
 * inventory.yaml next to the workflow when that is NULL and the file exists),
 * and makes it the one jobs read from. With no inventory the targets are just
 * `local`. Returns 0, or QWE_EXIT_USAGE after printing every error. */
static int load_inventory(lua_State *L, const char *cmd, const char *wf_path, const char *inv_path)
{
	char err[256], *yaml = NULL, *default_path = NULL;
	size_t yaml_len, cbor_len;
	uint8_t *cbor;
	struct qwe_positions *pos = NULL;
	int errors, top = lua_gettop(L);

	if (!inv_path) {
		const char *slash = strrchr(wf_path, '/');
		size_t dir_len = slash ? (size_t)(slash - wf_path) + 1 : 0;

		default_path = malloc(dir_len + sizeof "inventory.yaml");
		if (!default_path) {
			fprintf(stderr, "%s: %s: out of memory\n", cmd, wf_path);
			goto fail;
		}
		memcpy(default_path, wf_path, dir_len);
		strcpy(default_path + dir_len, "inventory.yaml");
		if (access(default_path, F_OK) != 0) {
			free(default_path);
			return 0;
		}
		inv_path = default_path;
	}
	if (read_file(inv_path, &yaml, &yaml_len) < 0) {
		fprintf(stderr, "%s: cannot read %s: %s\n", cmd, inv_path, strerror(errno));
		goto fail;
	}
	if (qwe_yaml_to_cbor(yaml, yaml_len, &cbor, &cbor_len, &pos, err, sizeof err) < 0) {
		fprintf(stderr, "%s: %s:%s\n", cmd, inv_path, err);
		free(yaml);
		goto fail;
	}
	free(yaml);
	if (qwe_cbor_to_lua(L, cbor, cbor_len, err, sizeof err) < 0) {
		fprintf(stderr, "%s: %s: %s\n", cmd, inv_path, err);
		free(cbor);
		qwe_positions_free(pos);
		goto fail;
	}
	free(cbor);
	if (!lua_istable(L, -1)) {
		fprintf(stderr, "%s: %s:1:1: an inventory is a map with targets: and secrets:\n", cmd, inv_path);
		qwe_positions_free(pos);
		goto fail;
	}
	errors = qwe_validate_inventory(L, cmd, inv_path, pos);
	qwe_positions_free(pos);
	if (errors > 0)
		goto fail;
	/* qwe.inventory.use(inventory): fully-vendored bootstrap Lua (src/kernel/lua/
	 * inventory.lua), never project-supplied -- lua_call, not lua_pcall (ticket 18). */
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.inventory");
	lua_call(L, 1, 1);
	lua_getfield(L, -1, "use");
	lua_pushvalue(L, top + 1);
	lua_call(L, 1, 0);
	lua_settop(L, top);
	free(default_path);
	return 0;
fail:
	free(default_path);
	lua_settop(L, top);
	return QWE_EXIT_USAGE;
}

/* Reads, transcodes, decodes and validates the workflow. On success returns 0
 * with the decoded workflow on top of *L's stack. Otherwise it has printed
 * every error (as file:line:col) and returns QWE_EXIT_USAGE, and *L is closed. */
static int load_workflow(const char *cmd, const char *path, const char *inventory, lua_State **L_out)
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
	if (load_inventory(L, cmd, path, inventory) != 0) {
		qwe_positions_free(pos);
		lua_close(L);
		return QWE_EXIT_USAGE;
	}
	errors = qwe_validate_doc(L, cmd, path, pos);
	qwe_positions_free(pos);
	if (errors > 0) {
		lua_close(L);
		return QWE_EXIT_USAGE;
	}
	*L_out = L;
	return 0;
}

static struct job *find_job(struct job *jobs, size_t n, const char *id)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (strcmp(jobs[i].id, id) == 0)
			return &jobs[i];
	return NULL;
}

static void ev_add(struct run_ctx *ctx, const struct job *job, enum evkind kind, int fd, long step)
{
	struct epoll_event e;

	e.events = EPOLLIN;
	e.data.u64 = tag_make((size_t)(job - ctx->jobs), kind, step);
	epoll_ctl(ctx->ep, EPOLL_CTL_ADD, fd, &e);
}

static void ev_del(struct run_ctx *ctx, int fd)
{
	epoll_ctl(ctx->ep, EPOLL_CTL_DEL, fd, NULL);
}

/* The step-table fields the shell needs. */
static int step_coe(struct job *job, size_t i)
{
	lua_State *L = job->L;
	int coe;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)i + 1);
	lua_getfield(L, -1, "continue-on-error");
	coe = lua_toboolean(L, -1);
	lua_pop(L, 4);
	return coe;
}

static char *step_id(struct job *job, size_t i)
{
	lua_State *L = job->L;
	char *id;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)i + 1);
	lua_getfield(L, -1, "id");
	id = lua_isstring(L, -1) ? qwe_xstrdup(lua_tostring(L, -1)) : NULL;
	lua_pop(L, 4);
	return id;
}

/* The step's name:, or NULL: the run summary falls back to it when there is no id. */
static char *step_name(struct job *job, size_t i)
{
	lua_State *L = job->L;
	char *name;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)i + 1);
	lua_getfield(L, -1, "name");
	name = lua_isstring(L, -1) ? qwe_xstrdup(lua_tostring(L, -1)) : NULL;
	lua_pop(L, 4);
	return name;
}

/* "run", or the step's uses: value: the run summary's plugin column. */
static char *step_plugin(struct job *job, size_t i)
{
	lua_State *L = job->L;
	char *plugin;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)i + 1);
	lua_getfield(L, -1, "uses");
	plugin = qwe_xstrdup(lua_isstring(L, -1) ? lua_tostring(L, -1) : "run");
	lua_pop(L, 4);
	return plugin;
}

/* A resource the shell could not get: the job is owed a start-failed. The
 * operation and errno go to the trace with that event. */
static void owe_start_failed(struct job *job, const char *what, const char *op, int err)
{
	struct job_run *r = &job->run;

	fprintf(stderr, "qwe run: cannot start %s of job %s: %s failed: %s\n", what, job->id, op, strerror(err));
	r->fail_op = op;
	r->fail_errno = err;
	r->have_followup = 1;
	r->followup = QWE_LC_EV_START_FAILED;
}

/* CLOCK_MONOTONIC, in nanoseconds. */
static int64_t mono_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Milliseconds from start_ns to end_ns, never negative. */
static long mono_diff_ms(int64_t start_ns, int64_t end_ns)
{
	long ms = (long)((end_ns - start_ns) / 1000000);

	return ms < 0 ? 0 : ms;
}

/* Gives back what a started job holds. */
static void job_release(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;

	if (r->timer_ok) {
		ev_del(ctx, r->timer.fd);
		qwe_timer_close(&r->timer);
		r->timer_ok = 0;
	}
	if (r->sink_ok) {
		qwe_sink_close(&r->sink);
		r->sink_ok = 0;
	}
	if (r->ring_ok) {
		job->dropped = r->ring.dropped;
		qwe_ring_free(&r->ring);
		qwe_redactor_free(&r->red);
		r->ring_ok = 0;
	}
	if (r->outputs_ref != LUA_NOREF) {
		luaL_unref(job->L, LUA_REGISTRYINDEX, r->outputs_ref);
		r->outputs_ref = LUA_NOREF;
	}
	free(r->step_target);
	free(r->step_token);
	r->step_target = r->step_token = NULL;
	free(r->out_path);
	free(r->step_json);
	r->out_path = r->step_json = NULL;
	free(r->res_buf);
	r->res_buf = NULL;
	r->res_len = r->res_cap = 0;
}

/* Action: open the job's log, ring and timer, and arm its timeout. */
static void job_start(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	const char *op = NULL;
	int err = 0;
	size_t i;

	job->L = ctx->L;
	lua_newtable(job->L);
	r->outputs_ref = luaL_ref(job->L, LUA_REGISTRYINDEX);
	if (qwe_ring_init(&r->ring, QWE_RING_CAPACITY) < 0) {
		op = "ring";
		err = errno;
	} else {
		r->ring_ok = 1;
	}
	if (!op) {
		if (qwe_sink_open(&r->sink, job->id, ctx->run_dir, 1) < 0) {
			op = "log";
			err = errno;
		} else {
			r->sink_ok = 1;
		}
	}
	if (!op) {
		if (qwe_timer_open(&r->timer) < 0) {
			op = "timer";
			err = errno;
		} else {
			r->timer_ok = 1;
		}
	}
	if (op) {
		job_release(ctx, job);
		owe_start_failed(job, "the log", op, err);
		return;
	}
	job->started = time(NULL);
	job->mono_start_ns = mono_now_ns();
	job->steps = qwe_xcalloc(job->nsteps, sizeof *job->steps);
	for (i = 0; i < job->nsteps; i++)
		job->steps[i].duration_ms = -1;
	if (job->timeout_ms > 0)
		qwe_timer_arm(&r->timer, job->timeout_ms);
	ev_add(ctx, job, EV_JOB_TIMER, r->timer.fd, -1);
}

/* Makes the live step's table for the child: templates evaluated, env merged
 * (qwe.template.resolve). Returns the step's timeout in milliseconds (0 for
 * none), or -1 after printing why. Sets cur_ref. */
static long resolve_step(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	lua_State *L = job->L;
	int top = lua_gettop(L);
	long ms;

	free(r->step_target);
	free(r->step_token);
	r->step_target = NULL;
	r->step_token = NULL;
	if (asprintf(&r->step_token, "%s.%s.%lu", ctx->run_id, job->id, (unsigned long)r->cur) < 0)
		r->step_token = NULL;
	r->cur_ref = LUA_NOREF;
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.template");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	lua_getfield(L, -1, "resolve");
	lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->wf_ref);
	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_pushinteger(L, (lua_Integer)r->cur + 1);
	lua_rawgeti(L, LUA_REGISTRYINDEX, r->outputs_ref);
	if (r->out_path)
		lua_pushstring(L, r->out_path);
	else
		lua_pushnil(L);
	lua_pushstring(L, r->step_token ? r->step_token : "");
	if (lua_pcall(L, 6, 1, 0) != 0)
		goto fail;
	ms = qwe_timeout_ms_at(L, -1);
	/* a step on a remote target says which */
	lua_getfield(L, -1, "__qwe");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "target");
		if (lua_isstring(L, -1))
			r->step_target = qwe_xstrdup(lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	r->cur_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the step table */
	lua_settop(L, top);
	return ms;
fail:
	fprintf(stderr, "qwe run: job %s: cannot prepare step %lu: %s\n", job->id, (unsigned long)r->cur + 1,
		lua_tostring(L, -1));
	lua_settop(L, top);
	return -1;
}

/* Records the outputs table at index tbl for the live step (qwe.template.store):
 * later steps of the job can read them, and result.json gets them as JSON. */
static void store_outputs(struct job *job, int tbl)
{
	struct job_run *r = &job->run;
	lua_State *L = job->L;
	int top = lua_gettop(L);

	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.template");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto out;
	lua_getfield(L, -1, "store");
	lua_rawgeti(L, LUA_REGISTRYINDEX, r->outputs_ref);
	lua_rawgeti(L, LUA_REGISTRYINDEX, r->cur_ref);
	lua_pushvalue(L, tbl);
	if (lua_pcall(L, 3, 1, 0) == 0 && lua_isstring(L, -1)) {
		free(r->step_json);
		r->step_json = qwe_xstrdup(lua_tostring(L, -1));
	}
out:
	lua_settop(L, top);
}

/* A run: step's outputs: read back from its $QWE_OUTPUT file, which is then
 * deleted (qwe.template.take_output_file). */
static void collect_file_outputs(struct job *job)
{
	struct job_run *r = &job->run;
	lua_State *L = job->L;
	int top = lua_gettop(L);

	if (!r->out_path)
		return;
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.template");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto out;
	lua_getfield(L, -1, "take_output_file");
	lua_pushstring(L, r->out_path);
	if (lua_pcall(L, 1, 1, 0) == 0 && lua_istable(L, -1))
		store_outputs(job, lua_gettop(L));
out:
	lua_settop(L, top);
}

/* Calls backend.ssh.<fn>(target, ...) with the string arguments given, in the
 * parent. Returns 0, or -1 with the reason left in msg. */
static int ssh_call(struct job *job, const char *fn, const char *a, const char *b, char *msg, size_t msg_size)
{
	lua_State *L = job->L;
	int top = lua_gettop(L), rc = 0, nargs = 1;

	msg[0] = '\0';
	lua_getglobal(L, "require");
	lua_pushstring(L, "backend.ssh");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	lua_getfield(L, -1, fn);
	lua_pushstring(L, job->run.step_target);
	if (a) {
		lua_pushstring(L, a);
		nargs++;
	}
	if (b) {
		lua_pushstring(L, b);
		nargs++;
	}
	if (lua_pcall(L, nargs, 2, 0) != 0)
		goto fail;
	if (!lua_toboolean(L, -2)) {
		snprintf(msg, msg_size, "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "failed");
		rc = -1;
	}
	lua_settop(L, top);
	return rc;
fail:
	snprintf(msg, msg_size, "%s", lua_tostring(L, -1));
	lua_settop(L, top);
	return -1;
}

/* Makes sure the master of the live step's target is up, starting it again if it
 * died: the one place a running job blocks the loop on ssh, bounded by the
 * reconnect timeout. Returns 0, or -1 with the reason in msg; *refused is set when
 * that was a refusal (the socket directory) rather than an unreachable host. */
static int remote_ensure(struct job *job, char *msg, size_t msg_size, int *refused)
{
	lua_State *L = job->L;
	int top = lua_gettop(L), rc = 0;
	const char *host;

	*refused = 0;
	/* qwe.inventory.host(target): fully-vendored bootstrap Lua, never
	 * project-supplied -- lua_call, not lua_pcall (ticket 18). */
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.inventory");
	lua_call(L, 1, 1);
	lua_getfield(L, -1, "host");
	lua_pushstring(L, job->run.step_target);
	lua_call(L, 1, 1);
	host = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
	lua_getglobal(L, "require");
	lua_pushstring(L, "backend.ssh");
	if (lua_pcall(L, 1, 1, 0) != 0)
		goto fail;
	lua_getfield(L, -1, "ensure");
	lua_pushstring(L, job->run.step_target);
	lua_pushstring(L, host);
	lua_getfield(L, -4, "RECONNECT_TIMEOUT");
	if (lua_pcall(L, 3, 3, 0) != 0)
		goto fail;
	if (!lua_toboolean(L, -3)) {
		snprintf(msg, msg_size, "%s", lua_isstring(L, -2) ? lua_tostring(L, -2) : "failed");
		*refused = lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "refused") == 0;
		rc = -1;
	}
	lua_settop(L, top);
	return rc;
fail:
	snprintf(msg, msg_size, "%s", lua_tostring(L, -1));
	lua_settop(L, top);
	*refused = 0;
	return -1;
}

/* Whether the run has given up on the live step's target (see backend.ssh.unreachable). */
static int remote_unreachable(struct job *job)
{
	char msg[8];

	return job->run.step_target && ssh_call(job, "unreachable", NULL, NULL, msg, sizeof msg) == 0;
}

/* Stops what the live step started on its remote host: killing the local ssh
 * client would not (sshd does not signal a command without a tty). */
static void remote_kill(struct job *job, const char *signal)
{
	char msg[8];

	/* once the local client has exited, the command has ended too */
	if (job->run.step_target && job->run.step_token && !job->run.leader_reaped)
		ssh_call(job, "kill", job->run.step_token, signal, msg, sizeof msg);
}

/* Whether the live step lost its connection: it exited 255 (ssh's own error
 * status) and the target's master is gone. */
static int remote_lost(struct job *job, int status)
{
	char msg[8];

	if (!job->run.step_target || !WIFEXITED(status) || WEXITSTATUS(status) != 255)
		return 0;
	/* ssh_call reads a true result as success: alive() is asked the other way round */
	return ssh_call(job, "alive", NULL, NULL, msg, sizeof msg) != 0;
}

/* Action: fork step cur. The state is already step-running; if the fork or
 * one of its timers fails, the job is owed a start-failed. */
static void step_spawn(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	struct qwe_step_result *res = &job->steps[r->cur];
	lua_State *L = job->L;
	struct child_arg arg;
	const char *op = NULL;
	long step_ms;
	int err = 0;

	res->id = step_id(job, r->cur);
	res->name = step_name(job, r->cur);
	res->plugin = step_plugin(job, r->cur);
	free(r->out_path);
	free(r->step_json);
	r->step_json = NULL;
	if (asprintf(&r->out_path, "%s/%s.%lu.output", ctx->run_dir_abs, job->id, (unsigned long)r->cur) < 0)
		r->out_path = NULL;
	step_ms = resolve_step(ctx, job);
	if (step_ms < 0) {
		/* no step table was made, so there is nothing to run */
		op = "template";
		err = EINVAL;
		step_ms = 0;
	}

	res->started = time(NULL);
	r->step_mono_start_ns = mono_now_ns();
	r->live_step = (long)r->cur;
	r->leader_reaped = 0;
	r->res_len = 0;
	r->res_overflow = 0;
	r->step_changed = 1;
	arg.L = L;
	arg.step_ref = r->cur_ref;
	r->step_unreachable = 0;
	if (!op && r->step_target && !remote_unreachable(job)) {
		char msg[512];
		int refused;

		if (remote_ensure(job, msg, sizeof msg, &refused) < 0) {
			fprintf(stderr, "qwe run: job %s: target %s: %s\n", job->id, r->step_target, msg);
			if (refused) {
				op = "ssh-master";
				err = ECONNREFUSED;
			}
			/* otherwise the target is now given up on: the step is spawned anyway */
		}
	}
	/* A step on a target the run has given up on is spawned and fails at once: its
	 * ssh finds no socket and exits 255 without a connection (ProxyCommand=false). */
	if (!op && r->step_target)
		r->step_unreachable = remote_unreachable(job);
	if (op) {
		/* resolve_step failed */
	} else if (make_output_file(r->out_path) < 0) {
		op = "output-file";
		err = errno;
	} else if (qwe_timer_open(&r->step_timer) < 0) {
		op = "timer";
		err = errno;
	} else if (qwe_timer_open(&r->grace_timer) < 0) {
		op = "timer";
		err = errno;
		qwe_timer_close(&r->step_timer);
	} else if (qwe_proc_spawn(&r->proc, child_argv, &arg) < 0) {
		op = r->proc.fail_op;
		err = errno;
		qwe_timer_close(&r->step_timer);
		qwe_timer_close(&r->grace_timer);
	}
	if (op) {
		owe_start_failed(job, "a step", op, err);
		return;
	}
	if (step_ms > 0)
		qwe_timer_arm(&r->step_timer, step_ms);
	ev_add(ctx, job, EV_OUT, r->proc.out_fd, r->live_step);
	ev_add(ctx, job, EV_RESULT, r->proc.res_fd, r->live_step);
	ev_add(ctx, job, EV_STEP_TIMER, r->step_timer.fd, r->live_step);
	ev_add(ctx, job, EV_GRACE_TIMER, r->grace_timer.fd, r->live_step);
	r->proc_ok = 1;
}

/* The step's group is empty (or it never started): release what it held and
 * move the cursor past it. */
static void step_finish(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	struct qwe_step_result *res;

	if (r->live_step < 0)
		return;
	res = &job->steps[r->live_step];
	if (res->duration_ms < 0)
		res->duration_ms = mono_diff_ms(r->step_mono_start_ns, mono_now_ns());
	if (r->proc_ok) {
		/* The child is gone; whatever it wrote is already in the pipe. */
		drain(r->proc.out_fd, &r->ring, &r->sink, &r->red);
		drain_flush(&r->ring, &r->sink, &r->red);
		ev_del(ctx, r->proc.out_fd);
		close(r->proc.out_fd);
		ev_del(ctx, r->proc.res_fd);
		close(r->proc.res_fd);
		ev_del(ctx, r->step_timer.fd);
		ev_del(ctx, r->grace_timer.fd);
		qwe_timer_close(&r->step_timer);
		qwe_timer_close(&r->grace_timer);
		r->proc_ok = 0;
	}
	free(r->step_target);
	free(r->step_token);
	r->step_target = r->step_token = NULL;
	luaL_unref(job->L, LUA_REGISTRYINDEX, r->cur_ref);
	r->cur++;
	r->live_step = -1;
}

/* Action: how the live step ended, as the table decided. */
static void record_step(struct job *job, const struct qwe_lc_result *res)
{
	struct qwe_step_result *s = &job->steps[job->run.cur];

	s->ended = time(NULL);
	s->changed = job->run.step_changed;
	s->outputs_json = job->run.step_json;
	job->run.step_json = NULL;
	s->outcome = qwe_lc_step_outcome(res);
	s->reason = res->reason;
}

/* Action: the job is over. Steps that never ran are skipped. */
static void job_end(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	size_t i;

	step_finish(ctx, job);
	if (job->steps) {
		job->ended = time(NULL);
		job->duration_ms = mono_diff_ms(job->mono_start_ns, mono_now_ns());
		for (i = r->cur; i < job->nsteps; i++) {
			job->steps[i].id = step_id(job, i);
			job->steps[i].name = step_name(job, i);
			job->steps[i].plugin = step_plugin(job, i);
			job->steps[i].outcome = "skipped";
		}
	}
	job_release(ctx, job);
}

static void apply_action(struct run_ctx *ctx, struct job *job, enum qwe_lc_action a, const struct qwe_lc_result *res)
{
	struct job_run *r = &job->run;

	switch (a) {
	case QWE_LC_ACT_NONE:
		break;
	case QWE_LC_ACT_START_JOB:
		job_start(ctx, job);
		break;
	case QWE_LC_ACT_SPAWN_STEP:
		step_spawn(ctx, job);
		break;
	case QWE_LC_ACT_SIGTERM_GROUP:
		remote_kill(job, "TERM");
		qwe_proc_kill_group(&r->proc, SIGTERM);
		break;
	case QWE_LC_ACT_ARM_GRACE:
		qwe_timer_arm(&r->grace_timer, ctx->grace_ms);
		break;
	case QWE_LC_ACT_SIGKILL_GROUP:
		remote_kill(job, "KILL");
		qwe_proc_kill_group(&r->proc, SIGKILL);
		break;
	case QWE_LC_ACT_RECORD_STEP:
		record_step(job, res);
		break;
	case QWE_LC_ACT_RECORD_JOB:
		job->reason = res->reason;
		break;
	case QWE_LC_ACT_END_JOB:
		job_end(ctx, job);
		break;
	}
}

/* One event, through the stale filter and the table. This is the only place a
 * job's state changes. */
static void dispatch_one(struct run_ctx *ctx, struct job *job, enum qwe_lc_event ev, struct qwe_lc_payload pl,
			 long ev_step)
{
	struct job_run *r = &job->run;
	enum qwe_lc_state from = job->state;
	long shown = r->live_step >= 0 ? r->live_step : (job->steps && r->cur < job->nsteps ? (long)r->cur : -1);
	const char *evname = qwe_lc_event_name(ev);
	char detail[96];
	struct qwe_lc_result res;
	size_t i;

	if (ctx->trace.debug)
		qwe_trace_record(&ctx->trace, job->id, shown, from, evname, NULL, "event", NULL, NULL);
	if (qwe_lc_event_is_stale(from, ev, ev_step, r->live_step)) {
		qwe_trace_record(&ctx->trace, job->id, shown, from, evname, qwe_lc_state_name(from), "stale", NULL, NULL);
		return;
	}
	if (ev == QWE_LC_EV_CANCEL)
		r->cancel_told = 1;
	if (ev == QWE_LC_EV_JOB_TIMEOUT)
		r->timeout_told = 1;
	pl.carried = r->last_reason;
	ctx->trace.job = job->id;
	ctx->trace.step = shown;
	res = qwe_lc_lookup(from, ev, &pl);
	detail[0] = '\0';
	if (ev == QWE_LC_EV_START_FAILED)
		snprintf(detail, sizeof detail, "op=%s errno=%s", r->fail_op ? r->fail_op : "?",
			 qwe_errno_name(r->fail_errno));
	qwe_trace_record(&ctx->trace, job->id, shown, from, evname, qwe_lc_state_name(res.next),
			 res.kind == QWE_LC_IGNORE ? "ignore" : "transition", res.reason, detail[0] ? detail : NULL);
	job->state = res.next;
	if (res.reason)
		r->last_reason = res.reason;
	if (ev == QWE_LC_EV_GROUP_EMPTY)
		step_finish(ctx, job);
	for (i = 0; i < QWE_LC_MAX_ACTIONS; i++)
		apply_action(ctx, job, res.actions[i], &res);
}

/* What the job is owed while no step is live: the cursor's next move. */
static enum qwe_lc_event between_steps_event(struct run_ctx *ctx, struct job *job, struct qwe_lc_payload *pl)
{
	struct job_run *r = &job->run;

	/* A trigger may have fired that the loop has not read yet. */
	if (!r->cancel_told && poll_cancel(ctx))
		return QWE_LC_EV_CANCEL;
	if (r->timer_ok && !r->timeout_told && qwe_timer_expired(&r->timer))
		return QWE_LC_EV_JOB_TIMEOUT;
	if (r->cur < job->nsteps) {
		pl->coe = step_coe(job, r->cur);
		return QWE_LC_EV_NEXT_STEP;
	}
	return QWE_LC_EV_NO_MORE_STEPS;
}

/* Sends an event to a job, then whatever the job is owed until it is waiting
 * for the outside world again. */
static void job_send(struct run_ctx *ctx, struct job *job, enum qwe_lc_event ev, struct qwe_lc_payload pl,
		     long ev_step)
{
	struct job_run *r = &job->run;

	dispatch_one(ctx, job, ev, pl, ev_step);
	for (;;) {
		struct qwe_lc_payload p = {0, NULL, NULL};
		enum qwe_lc_event next;

		if (r->have_followup) {
			r->have_followup = 0;
			next = r->followup;
		} else if (job->state == QWE_LC_BETWEEN_STEPS) {
			next = between_steps_event(ctx, job, &p);
		} else {
			break;
		}
		dispatch_one(ctx, job, next, p, -1);
	}
}

static void job_send_plain(struct run_ctx *ctx, struct job *job, enum qwe_lc_event ev, long ev_step)
{
	struct qwe_lc_payload none = {0, NULL, NULL};

	job_send(ctx, job, ev, none, ev_step);
}

/* Whether the live step of the job is a uses: step. */
static int live_step_is_plugin(struct job *job)
{
	lua_State *L = job->L;
	int uses;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->run.cur_ref);
	uses = step_uses_plugin(L, -1);
	lua_pop(L, 1);
	return uses;
}

/* What a step child reported on its result pipe. */
enum msg_status { MSG_NONE, MSG_EXEC, MSG_OK, MSG_FAILED };

struct step_msg {
	enum msg_status status;
	const char *reason; /* MSG_FAILED: not-converged, become-denied, engine-error or plugin-error */
	int changed;
};

/* Reads the step's result message. It is absent if the child died before
 * sending one, and ignored if it is malformed or too big: a broken child
 * gets no say in the outcome. The reason is checked against what a plugin may
 * report, so no plugin can invent an outcome for the engine. */
static struct step_msg read_step_msg(struct job *job)
{
	struct job_run *r = &job->run;
	lua_State *L = job->L;
	struct step_msg m = {MSG_NONE, NULL, 1};
	char err[128];

	if (r->res_len == 0 || r->res_overflow)
		return m;
	if (qwe_cbor_to_lua(L, (const uint8_t *)r->res_buf, r->res_len, err, sizeof err) < 0)
		return m;
	if (lua_istable(L, -1)) {
		const char *status, *reason;

		lua_getfield(L, -1, "status");
		status = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
		if (strcmp(status, "exec") == 0)
			m.status = MSG_EXEC;
		else if (strcmp(status, "ok") == 0)
			m.status = MSG_OK;
		else if (strcmp(status, "failed") == 0)
			m.status = MSG_FAILED;
		lua_pop(L, 1);
		lua_getfield(L, -1, "reason");
		reason = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
		m.reason = strcmp(reason, "not-converged") == 0 ? "not-converged"
			: strcmp(reason, "become-denied") == 0 ? "become-denied"
			: strcmp(reason, "engine-error") == 0 ? "engine-error" : "plugin-error";
		lua_pop(L, 1);
		lua_getfield(L, -1, "changed");
		if (lua_isboolean(L, -1))
			m.changed = lua_toboolean(L, -1);
		lua_pop(L, 1);
		if (m.status == MSG_OK) {
			lua_getfield(L, -1, "outputs");
			if (lua_istable(L, -1))
				store_outputs(job, lua_gettop(L));
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	return m;
}

/* How a step's leader ending is told to the table: the event and its reason.
 * A run: step, or a run-like plugin (it sent "exec"), succeeds if the command
 * exited 0. A check/apply plugin succeeds only if it said so and exited 0; if
 * it failed, it says why; if it died without saying (a crash, a signal), it
 * failed with plugin-error. */
static enum qwe_lc_event leader_exit_event(struct job *job, int status, struct qwe_lc_payload *pl)
{
	struct job_run *r = &job->run;
	int exited_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	struct step_msg m;

	pl->reason = "exit-code";
	r->step_changed = 1; /* run: steps always count as changed */
	if (!live_step_is_plugin(job)) {
		/* a run: step says only "exec", unless sudo refused it before it ran */
		drain_result(r);
		m = read_step_msg(job);
		collect_file_outputs(job);
		if (m.status == MSG_FAILED) {
			r->step_changed = m.changed;
			pl->reason = m.reason;
			return QWE_LC_EV_LEADER_EXIT_FAIL;
		}
		if (r->step_unreachable && WIFEXITED(status) && WEXITSTATUS(status) == 255)
			pl->reason = "unreachable";
		else if (remote_lost(job, status))
			pl->reason = "connection-lost";
		return exited_ok ? QWE_LC_EV_LEADER_EXIT_OK : QWE_LC_EV_LEADER_EXIT_FAIL;
	}
	drain_result(r);
	m = read_step_msg(job);
	if (m.status == MSG_EXEC)
		return exited_ok ? QWE_LC_EV_LEADER_EXIT_OK : QWE_LC_EV_LEADER_EXIT_FAIL;
	if (m.status != MSG_NONE)
		r->step_changed = m.changed;
	if (m.status == MSG_OK && exited_ok)
		return QWE_LC_EV_LEADER_EXIT_OK;
	pl->reason = m.status == MSG_FAILED ? m.reason : "plugin-error";
	return QWE_LC_EV_LEADER_EXIT_FAIL;
}

/* Reaps everything that has exited. qwe is a subreaper, so that includes
 * processes reparented to it when their parent died, not only step leaders.
 * A leader's exit is an event. A step ends when its whole group is empty: a
 * group is checked after each batch of exits, because that is the only time
 * it can become empty. A process that left the group (setsid) is not seen
 * (design §9.3, the known M1 gap). */
static void reap_children(struct run_ctx *ctx)
{
	struct signalfd_siginfo si;
	size_t i;
	pid_t pid;
	int status;

	while (read(ctx->chld_fd, &si, sizeof si) == (ssize_t)sizeof si)
		;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		for (i = 0; i < ctx->njobs; i++) {
			struct job *job = &ctx->jobs[i];
			struct job_run *r = &job->run;
			struct qwe_lc_payload pl = {0, "exit-code", NULL};

			if (r->live_step < 0 || !r->proc_ok || r->leader_reaped || r->proc.pid != pid)
				continue;
			r->leader_reaped = 1;
			job_send(ctx, job, leader_exit_event(job, status, &pl), pl, r->live_step);
			break;
		}
		/* no match: an orphan of some step's group, reaped and forgotten */
	}
	for (i = 0; i < ctx->njobs; i++) {
		struct job *job = &ctx->jobs[i];
		struct job_run *r = &job->run;

		if (r->live_step >= 0 && r->proc_ok && r->leader_reaped && qwe_proc_group_empty(r->proc.pid))
			job_send_plain(ctx, job, QWE_LC_EV_GROUP_EMPTY, r->live_step);
	}
}

/* One epoll event for a job. The event may be stale (its step or its job has
 * ended within the same batch): the tag says which step it was opened for,
 * and the table's filter drops it. A timer is only read while it is open. */
static void job_event(struct run_ctx *ctx, struct job *job, enum evkind kind, long step)
{
	struct job_run *r = &job->run;
	int live = step == r->live_step && r->proc_ok;

	switch (kind) {
	case EV_OUT:
		if (live && drain(r->proc.out_fd, &r->ring, &r->sink, &r->red))
			ev_del(ctx, r->proc.out_fd);
		break;
	case EV_RESULT:
		if (live && drain_result(r))
			ev_del(ctx, r->proc.res_fd);
		break;
	case EV_STEP_TIMER:
		if (live && !qwe_timer_expired(&r->step_timer))
			break;
		job_send_plain(ctx, job, QWE_LC_EV_STEP_TIMEOUT, step);
		break;
	case EV_GRACE_TIMER:
		if (live && !qwe_timer_expired(&r->grace_timer))
			break;
		job_send_plain(ctx, job, QWE_LC_EV_GRACE_EXPIRED, step);
		break;
	case EV_JOB_TIMER:
		if (r->timer_ok && (r->timeout_told || !qwe_timer_expired(&r->timer)))
			break;
		job_send_plain(ctx, job, QWE_LC_EV_JOB_TIMEOUT, -1);
		break;
	default:
		break;
	}
}

/* Sends the operator's cancel, once it is requested, to every job not yet told.
 * Returns whether any job was told. */
static int broadcast_cancel(struct run_ctx *ctx)
{
	size_t i;
	int sent = 0;

	if (!ctx->cancel_requested)
		return 0;
	for (i = 0; i < ctx->njobs; i++)
		if (!ctx->jobs[i].run.cancel_told) {
			job_send_plain(ctx, &ctx->jobs[i], QWE_LC_EV_CANCEL, -1);
			sent = 1;
		}
	return sent;
}

/* The sessions the target takes at once (inventory max-sessions:, default 8). */
static long target_max_sessions(lua_State *L, const char *target)
{
	int top = lua_gettop(L);
	long cap = 8;

	/* qwe.inventory.max_sessions(target): fully-vendored bootstrap Lua, never
	 * project-supplied -- lua_call, not lua_pcall (ticket 18). */
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.inventory");
	lua_call(L, 1, 1);
	lua_getfield(L, -1, "max_sessions");
	lua_pushstring(L, target);
	lua_call(L, 1, 1);
	if (lua_isnumber(L, -1))
		cap = (long)lua_tonumber(L, -1);
	lua_settop(L, top);
	return cap;
}

/* The disabled: note of an inventory target, as a new string; NULL if it is in service. */
static char *target_disabled_note(lua_State *L, const char *target)
{
	int top = lua_gettop(L);
	char *note = NULL;

	if (strcmp(target, "local") == 0)
		return NULL;
	/* qwe.inventory.disabled(target): fully-vendored bootstrap Lua, never
	 * project-supplied -- lua_call, not lua_pcall (ticket 18). */
	lua_getglobal(L, "require");
	lua_pushstring(L, "qwe.inventory");
	lua_call(L, 1, 1);
	lua_getfield(L, -1, "disabled");
	lua_pushstring(L, target);
	lua_call(L, 1, 1);
	if (lua_isstring(L, -1))
		note = qwe_xstrdup(lua_tostring(L, -1));
	lua_settop(L, top);
	return note;
}

/* Records, on each job whose target the operator has marked out of service, that
 * target's note. Done before pre-connect, which must not contact such a target. */
static void note_disabled(struct run_ctx *ctx)
{
	size_t i;

	for (i = 0; i < ctx->njobs; i++)
		ctx->jobs[i].detail = target_disabled_note(ctx->L, ctx->jobs[i].target);
}

/* A job on such a target never starts: it is skipped, before anything is
 * scheduled, with the reason target-disabled. Its dependents then skip the way
 * they do behind any job that did not succeed. */
static void skip_disabled(struct run_ctx *ctx)
{
	size_t i;

	for (i = 0; i < ctx->njobs; i++) {
		struct qwe_lc_payload pl = {0, "target-disabled", NULL};

		if (ctx->jobs[i].detail)
			job_send(ctx, &ctx->jobs[i], QWE_LC_EV_SKIP, pl, -1);
	}
}

/* Whether any step of the job runs on the job's target (a step with `on: local` does not). */
static int job_uses_target(lua_State *L, const struct job *job)
{
	int top = lua_gettop(L), used = 0;
	size_t s, n;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	n = lua_istable(L, -1) ? lua_objlen(L, -1) : 0;
	for (s = 1; s <= n && !used; s++) {
		lua_rawgeti(L, -1, (int)s);
		lua_getfield(L, -1, "on");
		used = !(lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "local") == 0);
		lua_pop(L, 2);
	}
	lua_settop(L, top);
	return used;
}

/* Opens a master to every remote target the selected jobs use, one after another,
 * before the first job starts. Nothing is timed yet, so a slow host costs startup
 * time and cannot make another job's timeout late. A target that cannot be reached
 * is reported here and given up on: its jobs fail with reason unreachable. A job
 * whose steps all run `on: local` never needs its target, so it is not contacted. */
static void preconnect(struct run_ctx *ctx)
{
	lua_State *L = ctx->L;
	size_t i, k;

	for (i = 0; i < ctx->njobs; i++) {
		const char *target = ctx->jobs[i].target;
		int top = lua_gettop(L);
		const char *host;

		if (strcmp(target, "local") == 0 || ctx->jobs[i].detail || !job_uses_target(L, &ctx->jobs[i]))
			continue;
		for (k = 0; k < i; k++)
			if (strcmp(ctx->jobs[k].target, target) == 0 && job_uses_target(L, &ctx->jobs[k]))
				break;
		if (k < i)
			continue;
		/* qwe.inventory.host(target): fully-vendored bootstrap Lua, never
		 * project-supplied -- lua_call, not lua_pcall (ticket 18). */
		lua_getglobal(L, "require");
		lua_pushstring(L, "qwe.inventory");
		lua_call(L, 1, 1);
		lua_getfield(L, -1, "host");
		lua_pushstring(L, target);
		lua_call(L, 1, 1);
		host = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
		lua_getglobal(L, "require");
		lua_pushstring(L, "backend.ssh");
		if (lua_pcall(L, 1, 1, 0) == 0) {
			lua_getfield(L, -1, "ensure");
			lua_pushstring(L, target);
			lua_pushstring(L, host);
			if (lua_pcall(L, 2, 3, 0) != 0)
				fprintf(stderr, "qwe run: target %s: %s\n", target, lua_tostring(L, -1));
			else if (!lua_toboolean(L, -3) && !lua_isstring(L, -1)) /* a refusal is reported when its job starts */
				fprintf(stderr, "qwe run: target %s: %s\n", target, lua_tostring(L, -2));
		} else {
			fprintf(stderr, "qwe run: target %s: %s\n", target, lua_tostring(L, -1));
		}
		lua_settop(L, top);
	}
}

/* Ends the ssh masters this run started, if it started any. */
static void close_masters(lua_State *L)
{
	int top = lua_gettop(L);

	lua_getglobal(L, "require");
	lua_pushstring(L, "backend.ssh");
	if (lua_pcall(L, 1, 1, 0) == 0) {
		lua_getfield(L, -1, "close_all");
		lua_pcall(L, 0, 0, 0);
	}
	lua_settop(L, top);
}

/* Runs every job to a final state on one event loop. The loop turns fds and
 * signals into events, and the lifecycle table decides what each one means. A
 * job whose needs are not all success is skipped (the default join rule); at
 * most max_parallel jobs run at once, started in id order. An operator cancel
 * is sent to every job once. */
static int run_all(struct run_ctx *ctx, long max_parallel)
{
	struct job *jobs = ctx->jobs;
	size_t n = ctx->njobs;
	struct qwe_sched_job *sj = qwe_xcalloc(n, sizeof *sj);
	struct qwe_sched_event *evs = qwe_xcalloc(n, sizeof *evs);
	long *caps = qwe_xcalloc(n, sizeof *caps);
	char **group_names = qwe_xcalloc(n ? n : 1, sizeof *group_names);
	size_t ngroups = 0;
	struct epoll_event got[32];
	size_t i, k;
	int rc = 0;

	ctx->ep = epoll_create1(EPOLL_CLOEXEC);
	{
		struct epoll_event e;

		e.events = EPOLLIN;
		e.data.u64 = tag_make(TAG_NO_JOB, EV_CHILD, -1);
		epoll_ctl(ctx->ep, EPOLL_CTL_ADD, ctx->chld_fd, &e);
		e.data.u64 = tag_make(TAG_NO_JOB, EV_CANCEL, -1);
		epoll_ctl(ctx->ep, EPOLL_CTL_ADD, ctx->cancel_fd, &e);
	}
	for (i = 0; i < n; i++) {
		jobs[i].needs_idx = qwe_xcalloc(jobs[i].nneeds, sizeof(size_t));
		for (k = 0; k < jobs[i].nneeds; k++)
			jobs[i].needs_idx[k] = (size_t)(find_job(jobs, n, jobs[i].needs[k]) - jobs);
		sj[i].state = &jobs[i].state;
		sj[i].needs = jobs[i].needs_idx;
		sj[i].nneeds = jobs[i].nneeds;
		/* a job on a remote target holds one of that target's sessions while it runs */
		if (strcmp(jobs[i].target, "local") != 0) {
			size_t g;

			for (g = 0; g < ngroups; g++)
				if (strcmp(group_names[g], jobs[i].target) == 0)
					break;
			if (g == ngroups) {
				group_names[ngroups] = jobs[i].target;
				caps[ngroups++] = target_max_sessions(ctx->L, jobs[i].target);
			}
			sj[i].group = g + 1;
		}
	}

	skip_disabled(ctx);

	for (;;) {
		size_t running = 0, final = 0, got_n;
		int ne;

		/* A job started by the pass can read the cancel off its signalfd
		 * (between_steps_event), so the broadcast goes again after it:
		 * otherwise the other jobs would not hear it until the next wakeup. */
		broadcast_cancel(ctx);
		do {
			while ((got_n = qwe_sched_pass(sj, n, max_parallel, caps, evs)) > 0)
				for (i = 0; i < got_n; i++)
					job_send_plain(ctx, &jobs[evs[i].job], evs[i].event, -1);
		} while (broadcast_cancel(ctx));

		for (i = 0; i < n; i++) {
			final += qwe_lc_state_is_final(jobs[i].state);
			running += qwe_lc_state_is_running(jobs[i].state);
		}
		if (final == n)
			break;
		if (running == 0) {
			/* Jobs are left and none is running or can be started. Validation
			 * rejects a needs: cycle, so this means the graph was not what it
			 * checked: say so, because this line is all the operator will get. */
			fprintf(stderr, "qwe run: internal error: %lu of %lu jobs are neither finished nor runnable "
					"(their needs: never resolve)\n",
				(unsigned long)(n - final), (unsigned long)n);
			rc = -1;
			break;
		}

		ne = epoll_wait(ctx->ep, got, 32, -1);
		for (i = 0; ne > 0 && i < (size_t)ne; i++) {
			uint64_t tag = got[i].data.u64;
			size_t job = (size_t)(tag >> 32);
			enum evkind kind = (enum evkind)((tag >> 24) & 0xFF);
			long step = (long)(tag & 0xFFFFFF) - 1;

			if (kind == EV_CHILD)
				reap_children(ctx);
			else if (kind == EV_CANCEL)
				poll_cancel(ctx);
			else
				job_event(ctx, &jobs[job], kind, step);
		}
	}
	for (i = 0; i < n; i++)
		free(jobs[i].needs_idx);
	close(ctx->ep);
	free(caps);
	free(group_names);
	free(sj);
	free(evs);
	return rc;
}

/* --job: keeps the chosen jobs and everything they need, transitively, and drops
 * the rest from the list, so they are not run and not in result.json. An id that
 * names no job is an error. Returns 0, or -1 after printing why. */
static int select_jobs(struct job *jobs, long *n, const struct qwe_run_options *opts)
{
	size_t total = (size_t)*n, i, k, m, kept = 0;
	char *keep = calloc(total ? total : 1, 1);
	int changed;

	if (!keep) {
		fprintf(stderr, "qwe run: --job: out of memory\n");
		return -1;
	}

	for (i = 0; i < opts->njobs; i++) {
		struct job *j = find_job(jobs, total, opts->jobs[i]);

		if (!j) {
			fprintf(stderr, "qwe run: --job %s: the workflow has no such job\n", opts->jobs[i]);
			free(keep);
			return -1;
		}
		keep[j - jobs] = 1;
	}
	do {
		changed = 0;
		for (i = 0; i < total; i++)
			for (k = 0; keep[i] && k < jobs[i].nneeds; k++) {
				size_t need = (size_t)(find_job(jobs, total, jobs[i].needs[k]) - jobs);

				if (!keep[need]) {
					keep[need] = 1;
					changed = 1;
				}
			}
	} while (changed);
	for (i = 0; i < total; i++) {
		if (!keep[i]) {
			for (m = 0; m < jobs[i].nneeds; m++)
				free(jobs[i].needs[m]);
			free(jobs[i].needs);
			free(jobs[i].id);
			free(jobs[i].target);
			continue;
		}
		jobs[kept++] = jobs[i];
	}
	*n = (long)kept;
	free(keep);
	return 0;
}

/* One line at the end of a run that skipped jobs for disabled targets: how many
 * jobs, and which targets with their notes. A green run that did part of the work
 * must not look like one that did all of it. */
static void report_disabled(const struct job *jobs, size_t n)
{
	size_t i, k, skipped = 0, targets = 0;
	const struct job **seen = qwe_xcalloc(n, sizeof *seen);
	char *line = NULL;
	size_t len = 0;

	for (i = 0; i < n; i++) {
		if (!jobs[i].detail || !jobs[i].reason || strcmp(jobs[i].reason, "target-disabled") != 0)
			continue;
		skipped++;
		for (k = 0; k < targets; k++)
			if (strcmp(seen[k]->target, jobs[i].target) == 0)
				break;
		if (k == targets)
			seen[targets++] = &jobs[i];
	}
	if (skipped) {
		FILE *mem = open_memstream(&line, &len);

		if (mem) {
			fprintf(mem, "qwe run: %lu job%s skipped, target%s disabled:", (unsigned long)skipped,
				skipped == 1 ? "" : "s", targets == 1 ? "" : "s");
			for (k = 0; k < targets; k++)
				fprintf(mem, "%s %s (%s)", k ? "," : "", seen[k]->target, seen[k]->detail);
			fclose(mem);
			fprintf(stderr, "%s\n", line);
			free(line);
		}
	}
	free(seen);
}

int qwe_run_workflow(const char *path, const struct qwe_run_options *opts)
{
	char run_id[64], *dir = NULL, *run_dir = NULL, *trace_path = NULL;
	lua_State *L;
	struct job *jobs = NULL;
	struct run_ctx ctx;
	sigset_t cancel_set;
	const char *grace_env;
	struct qwe_job_result *results = NULL;
	const char *slash;
	long n, max_parallel;
	size_t i;
	int rc = QWE_EXIT_OK, all_ok = 1, trace_ok = 0;
	FILE *fp;
	int64_t run_mono_start;
	long run_duration_ms;

	memset(&ctx, 0, sizeof ctx);
	if (load_workflow("qwe run", path, opts ? opts->inventory : NULL, &L) != 0)
		return QWE_EXIT_USAGE;
	n = qwe_jobs_load(L, path, &jobs);
	if (n < 0) {
		lua_close(L);
		return QWE_EXIT_USAGE;
	}
	if (opts && opts->jobs && select_jobs(jobs, &n, opts) < 0) {
		rc = QWE_EXIT_USAGE;
		goto out;
	}

	/* .qwe/runs/<run-id>/ next to the workflow. */
	slash = strrchr(path, '/');
	dir = slash ? strndup(path, (size_t)(slash - path)) : strdup(".");
	fmt_run_id(run_id, sizeof run_id);
	run_dir = dir ? malloc(strlen(dir) + strlen(run_id) + 32) : NULL;
	if (!run_dir) {
		fprintf(stderr, "qwe run: %s: out of memory\n", path);
		rc = QWE_EXIT_USAGE;
		goto out;
	}
	sprintf(run_dir, "%s/.qwe/runs/%s", dir, run_id);
	if (mkdir_p(run_dir, 0700) < 0) {
		fprintf(stderr, "qwe run: cannot create %s: %s\n", run_dir, strerror(errno));
		rc = QWE_EXIT_USAGE;
		goto out;
	}
	trace_path = malloc(strlen(run_dir) + 32);
	if (!trace_path) {
		fprintf(stderr, "qwe run: %s: out of memory\n", path);
		rc = QWE_EXIT_USAGE;
		goto out;
	}
	sprintf(trace_path, "%s/lifecycle.trace", run_dir);
	if (qwe_trace_open(&ctx.trace, trace_path, opts && opts->debug) < 0) {
		fprintf(stderr, "qwe run: cannot create %s: %s\n", trace_path, strerror(errno));
		rc = QWE_EXIT_USAGE;
		goto out;
	}
	trace_ok = 1;
	qwe_trace_install_abort_hook(&ctx.trace);

	/* SIGCHLD, SIGINT and SIGTERM are read through signalfds, so they must be
	 * blocked. Children start with an empty mask (see proc.c). */
	sigemptyset(&ctx.chld_mask);
	sigaddset(&ctx.chld_mask, SIGCHLD);
	sigemptyset(&cancel_set);
	sigaddset(&cancel_set, SIGINT);
	sigaddset(&cancel_set, SIGTERM);
	sigprocmask(SIG_BLOCK, &ctx.chld_mask, NULL);
	sigprocmask(SIG_BLOCK, &cancel_set, NULL);
	/* A step's orphans come back to qwe, so that a step ends only when its whole
	 * group is empty. */
	if (qwe_proc_become_subreaper() < 0)
		fprintf(stderr, "qwe run: warning: cannot become a subreaper: %s\n", strerror(errno));
	ctx.cancel_fd = signalfd(-1, &cancel_set, SFD_CLOEXEC | SFD_NONBLOCK);
	ctx.chld_fd = signalfd(-1, &ctx.chld_mask, SFD_CLOEXEC | SFD_NONBLOCK);
	/* The grace period is fixed at 10 seconds; the override is for tests only. */
	grace_env = getenv("QWE_TEST_GRACE_MS");
	ctx.grace_ms = grace_env ? atol(grace_env) : 10000;
	ctx.jobs = jobs;
	ctx.njobs = (size_t)n;
	ctx.run_dir = run_dir;
	ctx.run_id = run_id;
	ctx.L = L;

	ctx.run_dir_abs = realpath(run_dir, NULL);
	if (!ctx.run_dir_abs)
		ctx.run_dir_abs = qwe_xstrdup(run_dir);
	lua_pushvalue(L, -1);
	ctx.wf_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "max-parallel");
	max_parallel = lua_isnumber(L, -1) ? (long)lua_tonumber(L, -1) : 0;
	lua_pop(L, 1);
	note_disabled(&ctx);
	preconnect(&ctx);
	run_mono_start = mono_now_ns();
	if (run_all(&ctx, max_parallel) < 0)
		rc = QWE_EXIT_FAILED;
	close_masters(L);
	qwe_redact_clear();
	run_duration_ms = mono_diff_ms(run_mono_start, mono_now_ns());

	results = qwe_xcalloc((size_t)n, sizeof *results);
	for (i = 0; i < (size_t)n; i++) {
		results[i].id = jobs[i].id;
		results[i].outcome = qwe_lc_state_name(jobs[i].state);
		results[i].reason = jobs[i].reason;
		results[i].detail = jobs[i].detail;
		results[i].started = jobs[i].started;
		results[i].ended = jobs[i].ended;
		results[i].duration_ms = jobs[i].duration_ms;
		results[i].dropped_bytes = jobs[i].dropped;
		results[i].steps = jobs[i].steps;
		results[i].nsteps = jobs[i].steps ? jobs[i].nsteps : 0;
		/* a run is ok when every job is success or skipped */
		if (jobs[i].state != QWE_LC_SUCCESS && jobs[i].state != QWE_LC_SKIPPED)
			all_ok = 0;
	}

	if (!all_ok)
		rc = QWE_EXIT_FAILED;
	if (ctx.cancel_requested)
		rc = QWE_EXIT_CANCELLED; /* the operator's cancel outranks any job outcome */

	strcat(run_dir, "/result.json");
	fp = fopen(run_dir, "w");
	if (!fp || qwe_result_write(fp, run_id, results, (size_t)n) < 0 || fclose(fp) != 0) {
		fprintf(stderr, "qwe run: cannot write %s: %s\n", run_dir, strerror(errno));
		rc = QWE_EXIT_FAILED;
	}
	if (opts && opts->summary) {
		const char *outcome = rc == QWE_EXIT_CANCELLED ? "cancelled" : all_ok ? "success" : "failed";

		if (qwe_summary_write(opts->summary, ctx.run_dir_abs, path, outcome, run_duration_ms, results,
				      (size_t)n) < 0)
			fprintf(stderr, "qwe run: cannot write summary %s: %s\n", opts->summary, strerror(errno));
	}
	report_disabled(jobs, (size_t)n);
	qwe_lc_set_abort_hook(NULL, NULL);

out:
	if (trace_ok)
		qwe_trace_close(&ctx.trace);
	free(ctx.run_dir_abs);
	qwe_jobs_free(L, jobs, (size_t)n);
	lua_close(L);
	free(results);
	free(trace_path);
	free(run_dir);
	free(dir);
	return rc;
}

int qwe_validate_workflow(const char *path, const char *inventory)
{
	lua_State *L;
	int rc = load_workflow("qwe validate", path, inventory, &L);

	if (rc == 0)
		lua_close(L);
	return rc == 0 ? QWE_EXIT_OK : rc;
}
