#define _GNU_SOURCE
#include "src/kernel/qwe.h"

#include "src/edge/yaml/transcode.h"
#include "src/kernel/job_state.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"
#include "src/kernel/validate.h"
#include "src/kernel/proc.h"
#include "src/kernel/timer.h"
#include "src/kernel/result.h"
#include "src/kernel/ring.h"
#include "src/kernel/sched.h"
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

/* What an epoll event is about. Every fd in the loop carries one of these as
 * its tag, so the one loop can tell jobs apart. */
enum evkind { EV_OUT, EV_STEP_TIMER, EV_JOB_TIMER, EV_GRACE_TIMER, EV_CHILD, EV_CANCEL };

struct job;

struct ev {
	struct job *job; /* NULL for EV_CHILD and EV_CANCEL */
	enum evkind kind;
};

/* What is shared by every job of a run. */
struct run_ctx {
	sigset_t chld_mask;
	int ep;               /* the one epoll every job's fds are on */
	int chld_fd;          /* signalfd for SIGCHLD */
	struct ev chld_ev, cancel_ev;
	int cancel_fd;        /* signalfd for SIGINT and SIGTERM */
	int cancel_requested; /* sticky: the operator asked to stop */
	long grace_ms;        /* SIGTERM to SIGKILL */
};

/* Why a step is being torn down. A later trigger of a higher value wins the
 * outcome, but never restarts the teardown: the grace timer is armed once. */
enum trigger { TRIG_NONE, TRIG_STEP_TIMEOUT, TRIG_JOB_TIMEOUT, TRIG_CANCEL };

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

static long timeout_ms_at(lua_State *L, int idx);

/* What one live job needs beyond its result: everything that used to sit on
 * the stack of a blocking run_job. */
struct job_run {
	struct qwe_ring ring;
	struct qwe_sink sink;
	struct qwe_timer timer;      /* the job's own limit */
	struct qwe_timer step_timer; /* the live step's limit */
	struct qwe_timer grace_timer;
	struct qwe_proc proc;
	size_t cur;                  /* the step running now, or the next to run */
	int cur_ref;                 /* registry ref of the live step's table */
	int cur_continue;            /* the live step's continue-on-error */
	int step_live;               /* a process is running for step cur */
	int tearing;                 /* SIGTERM has been sent to the live step */
	enum trigger step_trigger;   /* the strongest trigger that hit the live step */
	enum trigger torn;           /* the job-level trigger that ended the job, if any */
	int failed;
};

/* One job as the scheduler sees it. */
struct job {
	char *id;
	int ref; /* registry ref of the job's Lua table */
	enum qwe_job_state state;
	const char *reason;
	char **needs;
	size_t nneeds;
	struct qwe_step_result *steps;
	size_t nsteps;
	time_t started, ended;
	unsigned long dropped;
	long timeout_ms; /* the job's own limit, 0 for none */
	lua_State *L;
	struct job_run run;
	struct ev ev[4]; /* tags for this job's fds, indexed by evkind */
	size_t *needs_idx;
};

static int cmp_job(const void *a, const void *b)
{
	return strcmp(((const struct job *)a)->id, ((const struct job *)b)->id);
}

/* Keys the engine accepts in the schema but does not act on yet. Running a
 * workflow that uses one would silently do the wrong thing, so it is refused. */
static const char *const unimplemented_job_keys[] = {"env", NULL};
static const char *const unimplemented_step_keys[] = {"env", "become", "on", "secret-outputs", "with", NULL};

static int refuse(const char *path, const char *what, const char *id, const char *key, const char *why)
{
	fprintf(stderr, "qwe run: %s: %s %s: %s: %s\n", path, what, id, key, why);
	return -1;
}

/* Reads the jobs out of the decoded workflow (on top of L's stack) into a
 * list sorted by id, and refuses what the engine cannot run yet. Returns the
 * job count, or -1 after printing why. */
static long load_jobs(lua_State *L, const char *path, struct job **out)
{
	struct job *jobs = NULL;
	size_t n = 0, cap = 0, i, k;
	int bad = 0;

	lua_getfield(L, -1, "env");
	if (!lua_isnil(L, -1))
		bad = refuse(path, "workflow", "", "env", "not implemented yet");
	lua_pop(L, 1);
	lua_getfield(L, -1, "jobs");
	lua_pushnil(L);
	while (!bad && lua_next(L, -2)) {
		if (n == cap)
			jobs = realloc(jobs, (cap = cap ? cap * 2 : 8) * sizeof *jobs);
		memset(&jobs[n], 0, sizeof jobs[n]);
		lua_pushvalue(L, -2);
		jobs[n].id = strdup(lua_tostring(L, -1));
		lua_pop(L, 1);
		jobs[n].ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the job table; the key stays */
		n++;
	}
	lua_pop(L, 1); /* jobs */
	if (bad)
		return -1;
	qsort(jobs, n, sizeof *jobs, cmp_job);

	for (i = 0; i < n; i++) {
		struct job *j = &jobs[i];
		size_t nsteps;

		lua_rawgeti(L, LUA_REGISTRYINDEX, j->ref);
		lua_getfield(L, -1, "target");
		if (!lua_isstring(L, -1) || strcmp(lua_tostring(L, -1), "local") != 0)
			bad = refuse(path, "job", j->id, "target", "only target: local is implemented");
		lua_pop(L, 1);
		for (k = 0; unimplemented_job_keys[k]; k++) {
			lua_getfield(L, -1, unimplemented_job_keys[k]);
			if (!lua_isnil(L, -1))
				bad = refuse(path, "job", j->id, unimplemented_job_keys[k], "not implemented yet");
			lua_pop(L, 1);
		}
		j->timeout_ms = timeout_ms_at(L, -1);
		lua_getfield(L, -1, "needs");
		if (lua_istable(L, -1)) {
			j->nneeds = lua_objlen(L, -1);
			j->needs = calloc(j->nneeds ? j->nneeds : 1, sizeof *j->needs);
			for (k = 0; k < j->nneeds; k++) {
				lua_rawgeti(L, -1, (int)k + 1);
				j->needs[k] = strdup(lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
		lua_getfield(L, -1, "steps");
		nsteps = lua_objlen(L, -1);
		for (k = 1; k <= nsteps; k++) {
			size_t m;

			lua_rawgeti(L, -1, (int)k);
			lua_getfield(L, -1, "run");
			if (!lua_isstring(L, -1))
				bad = refuse(path, "job", j->id, "uses", "plugin steps are not implemented yet");
			lua_pop(L, 1);
			for (m = 0; unimplemented_step_keys[m]; m++) {
				lua_getfield(L, -1, unimplemented_step_keys[m]);
				if (!lua_isnil(L, -1))
					bad = refuse(path, "job", j->id, unimplemented_step_keys[m], "not implemented yet");
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 2); /* steps, job */
		j->nsteps = nsteps;
	}
	if (bad)
		return -1;
	*out = jobs;
	return (long)n;
}

static struct job *find_job(struct job *jobs, size_t n, const char *id)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (strcmp(jobs[i].id, id) == 0)
			return &jobs[i];
	return NULL;
}

/* Milliseconds for a timeout-minutes value; 0 (no limit) if it is absent. */
static long timeout_ms_at(lua_State *L, int idx)
{
	long ms = 0;

	lua_getfield(L, idx, "timeout-minutes");
	if (lua_isnumber(L, -1))
		ms = (long)(lua_tonumber(L, -1) * 60000.0);
	lua_pop(L, 1);
	return ms > 0 ? ms : 0;
}

static void ev_add(struct run_ctx *ctx, struct job *job, enum evkind kind, int fd)
{
	struct epoll_event e;

	job->ev[kind].job = job;
	job->ev[kind].kind = kind;
	e.events = EPOLLIN;
	e.data.ptr = &job->ev[kind];
	epoll_ctl(ctx->ep, EPOLL_CTL_ADD, fd, &e);
}

static void ev_del(struct run_ctx *ctx, int fd)
{
	epoll_ctl(ctx->ep, EPOLL_CTL_DEL, fd, NULL);
}

/* Applies a trigger to the job's live step. Teardown is one path: SIGTERM to
 * the process group, then SIGKILL when the grace timer fires. A later trigger
 * of a higher value wins the outcome but never restarts the teardown: the
 * grace timer is armed once. */
static void step_trigger(struct run_ctx *ctx, struct job *job, enum trigger now)
{
	struct job_run *r = &job->run;

	if (!r->step_live)
		return;
	if (now > r->step_trigger)
		r->step_trigger = now;
	if (!r->tearing) {
		r->tearing = 1;
		qwe_proc_kill_group(&r->proc, SIGTERM);
		qwe_timer_arm(&r->grace_timer, ctx->grace_ms);
	}
}

/* Records how step cur ended and moves on to the next. */
static void step_record(struct job *job, enum trigger t, int status)
{
	struct job_run *r = &job->run;
	struct qwe_step_result *res = &job->steps[r->cur];

	res->ended = time(NULL);
	res->changed = 1; /* run: steps always count as changed */
	if (t == TRIG_JOB_TIMEOUT || t == TRIG_CANCEL) {
		/* Imposed from outside: no continue-on-error can save it. */
		res->outcome = "cancelled";
		res->reason = t == TRIG_CANCEL ? "cancel-requested" : "timeout";
		r->torn = t;
	} else if (t == TRIG_STEP_TIMEOUT) {
		/* The author's own limit on this step: a failure. */
		res->outcome = "failed";
		res->reason = "timeout";
		if (!r->cur_continue) {
			r->failed = 1;
			job->reason = res->reason;
		}
	} else if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		res->outcome = "success";
	} else {
		res->outcome = "failed";
		res->reason = "exit-code";
		if (!r->cur_continue) {
			r->failed = 1;
			job->reason = res->reason;
		}
	}
	luaL_unref(job->L, LUA_REGISTRYINDEX, r->cur_ref);
	r->cur++;
}

/* The live step's process has been reaped: finish it. */
static void step_reap(struct run_ctx *ctx, struct job *job, int status)
{
	struct job_run *r = &job->run;

	/* Whatever the group left behind after its leader went is not wanted: a
	 * torn-down step must leave nothing alive. */
	if (r->tearing)
		qwe_proc_kill_group(&r->proc, SIGKILL);
	/* The child is gone; whatever it wrote is already in the pipe. */
	drain(r->proc.out_fd, &r->ring, &r->sink);
	ev_del(ctx, r->proc.out_fd);
	close(r->proc.out_fd);
	ev_del(ctx, r->step_timer.fd);
	ev_del(ctx, r->grace_timer.fd);
	qwe_timer_close(&r->step_timer);
	qwe_timer_close(&r->grace_timer);
	r->step_live = 0;
	step_record(job, r->step_trigger, status);
}

/* Starts step cur, or records it skipped because the job has already ended.
 * Either way it leaves the step done unless step_live is set. */
static void step_begin(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	struct qwe_step_result *res = &job->steps[r->cur];
	lua_State *L = job->L;
	struct child_arg arg;
	long step_ms;

	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)r->cur + 1);
	lua_getfield(L, -1, "id");
	res->id = lua_isstring(L, -1) ? strdup(lua_tostring(L, -1)) : NULL;
	lua_pop(L, 1);
	lua_getfield(L, -1, "continue-on-error");
	r->cur_continue = lua_toboolean(L, -1);
	lua_pop(L, 1);
	step_ms = timeout_ms_at(L, -1);
	r->cur_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the step table */
	lua_pop(L, 2);

	/* Between steps: a trigger may already have fired. */
	if (!r->failed && r->torn == TRIG_NONE) {
		if (poll_cancel(ctx))
			r->torn = TRIG_CANCEL;
		else if (qwe_timer_expired(&r->timer))
			r->torn = TRIG_JOB_TIMEOUT;
	}
	if (r->failed || r->torn != TRIG_NONE) {
		/* The job has ended before this step started. */
		res->outcome = "skipped";
		luaL_unref(L, LUA_REGISTRYINDEX, r->cur_ref);
		r->cur++;
		return;
	}

	res->started = time(NULL);
	r->step_trigger = TRIG_NONE;
	r->tearing = 0;
	arg.L = L;
	arg.step_ref = r->cur_ref;
	if (qwe_timer_open(&r->step_timer) < 0)
		goto failed_to_start;
	if (qwe_timer_open(&r->grace_timer) < 0) {
		qwe_timer_close(&r->step_timer);
		goto failed_to_start;
	}
	if (qwe_proc_spawn(&r->proc, child_argv, &arg) < 0) {
		qwe_timer_close(&r->step_timer);
		qwe_timer_close(&r->grace_timer);
		goto failed_to_start;
	}
	if (step_ms > 0)
		qwe_timer_arm(&r->step_timer, step_ms);
	ev_add(ctx, job, EV_OUT, r->proc.out_fd);
	ev_add(ctx, job, EV_STEP_TIMER, r->step_timer.fd);
	ev_add(ctx, job, EV_GRACE_TIMER, r->grace_timer.fd);
	r->step_live = 1;
	return;

failed_to_start:
	step_record(job, TRIG_NONE, -1);
}

/* Records the job's final state and releases what it held. */
static void job_finish(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;

	job->ended = time(NULL);
	job->dropped = r->ring.dropped;
	if (r->torn != TRIG_NONE) {
		qwe_job_transition(&job->state, QWE_JOB_TERMINATING);
		qwe_job_transition(&job->state, QWE_JOB_CANCELLED);
		job->reason = r->torn == TRIG_CANCEL ? "cancel-requested" : "timeout";
	} else {
		qwe_job_transition(&job->state, r->failed ? QWE_JOB_FAILED : QWE_JOB_SUCCESS);
	}
	ev_del(ctx, r->timer.fd);
	qwe_timer_close(&r->timer);
	qwe_sink_close(&r->sink);
	qwe_ring_free(&r->ring);
}

/* Runs steps until one is live or none are left, in which case the job ends. */
static void job_advance(struct run_ctx *ctx, struct job *job)
{
	while (!job->run.step_live && job->run.cur < job->nsteps)
		step_begin(ctx, job);
	if (!job->run.step_live)
		job_finish(ctx, job);
}

/* ready -> running. A job whose log cannot be opened fails on the spot. */
static void job_begin(struct run_ctx *ctx, lua_State *L, struct job *job, const char *run_dir)
{
	struct job_run *r = &job->run;

	memset(r, 0, sizeof *r);
	job->L = L;
	qwe_job_transition(&job->state, QWE_JOB_RUNNING);
	if (qwe_ring_init(&r->ring, QWE_RING_CAPACITY) < 0 || qwe_sink_open(&r->sink, job->id, run_dir, 1) < 0 ||
	    qwe_timer_open(&r->timer) < 0) {
		fprintf(stderr, "qwe run: cannot open the log of job %s in %s: %s\n", job->id, run_dir, strerror(errno));
		qwe_job_transition(&job->state, QWE_JOB_FAILED);
		job->reason = "internal";
		return;
	}
	job->started = time(NULL);
	job->steps = calloc(job->nsteps ? job->nsteps : 1, sizeof *job->steps);
	if (job->timeout_ms > 0)
		qwe_timer_arm(&r->timer, job->timeout_ms);
	ev_add(ctx, job, EV_JOB_TIMER, r->timer.fd);
	job_advance(ctx, job);
}

/* Reaps every live step whose process has exited. */
static void reap_children(struct run_ctx *ctx, struct job *jobs, size_t n)
{
	struct signalfd_siginfo si;
	size_t i;

	while (read(ctx->chld_fd, &si, sizeof si) == (ssize_t)sizeof si)
		;
	for (i = 0; i < n; i++) {
		int status;

		if (jobs[i].state != QWE_JOB_RUNNING || !jobs[i].run.step_live)
			continue;
		if (waitpid(jobs[i].run.proc.pid, &status, WNOHANG) == jobs[i].run.proc.pid) {
			step_reap(ctx, &jobs[i], status);
			job_advance(ctx, &jobs[i]);
		}
	}
}

/* One epoll event for a job. The event may be stale (the step it was about
 * has ended within the same batch); every branch checks the live state. */
static void job_event(struct run_ctx *ctx, struct job *job, enum evkind kind)
{
	struct job_run *r = &job->run;

	if (job->state != QWE_JOB_RUNNING || !r->step_live)
		return;
	switch (kind) {
	case EV_OUT:
		if (drain(r->proc.out_fd, &r->ring, &r->sink))
			ev_del(ctx, r->proc.out_fd);
		break;
	case EV_STEP_TIMER:
		if (qwe_timer_expired(&r->step_timer))
			step_trigger(ctx, job, TRIG_STEP_TIMEOUT);
		break;
	case EV_JOB_TIMER:
		if (qwe_timer_expired(&r->timer))
			step_trigger(ctx, job, TRIG_JOB_TIMEOUT);
		break;
	case EV_GRACE_TIMER:
		if (qwe_timer_expired(&r->grace_timer))
			qwe_proc_kill_group(&r->proc, SIGKILL);
		break;
	default:
		break;
	}
}

/* Runs every job to a final state on one event loop. A job whose needs are
 * not all success is skipped (the default join rule); at most max_parallel
 * jobs run at once, started in id order. An operator cancel tears down what
 * is running and skips every job still pending. */
static int run_all(struct run_ctx *ctx, lua_State *L, struct job *jobs, size_t n, const char *run_dir,
		   long max_parallel)
{
	struct qwe_sched_job *sj = calloc(n ? n : 1, sizeof *sj);
	size_t *starts = calloc(n ? n : 1, sizeof *starts);
	struct epoll_event got[32];
	size_t i, k;
	int rc = 0;

	ctx->ep = epoll_create1(EPOLL_CLOEXEC);
	ctx->chld_ev.kind = EV_CHILD;
	ctx->cancel_ev.kind = EV_CANCEL;
	{
		struct epoll_event e;

		e.events = EPOLLIN;
		e.data.ptr = &ctx->chld_ev;
		epoll_ctl(ctx->ep, EPOLL_CTL_ADD, ctx->chld_fd, &e);
		e.data.ptr = &ctx->cancel_ev;
		epoll_ctl(ctx->ep, EPOLL_CTL_ADD, ctx->cancel_fd, &e);
	}
	for (i = 0; i < n; i++) {
		jobs[i].needs_idx = calloc(jobs[i].nneeds ? jobs[i].nneeds : 1, sizeof(size_t));
		for (k = 0; k < jobs[i].nneeds; k++)
			jobs[i].needs_idx[k] = (size_t)(find_job(jobs, n, jobs[i].needs[k]) - jobs);
		sj[i].state = &jobs[i].state;
		sj[i].needs = jobs[i].needs_idx;
		sj[i].nneeds = jobs[i].nneeds;
	}

	for (;;) {
		size_t started, live = 0, final = 0;
		int ne;

		if (ctx->cancel_requested) {
			for (i = 0; i < n; i++) {
				if (jobs[i].state == QWE_JOB_PENDING) {
					qwe_job_transition(&jobs[i].state, QWE_JOB_SKIPPED);
					jobs[i].reason = "cancel-requested";
				} else if (jobs[i].state == QWE_JOB_RUNNING) {
					step_trigger(ctx, &jobs[i], TRIG_CANCEL);
				}
			}
		}
		started = qwe_sched_pass(sj, n, max_parallel, starts);
		for (i = 0; i < n; i++)
			if (jobs[i].state == QWE_JOB_SKIPPED && !jobs[i].reason)
				jobs[i].reason = "dependency-failed";
		for (i = 0; i < started; i++)
			job_begin(ctx, L, &jobs[starts[i]], run_dir);
		if (started > 0)
			continue; /* a job that ended at once may unlock others */

		for (i = 0; i < n; i++) {
			final += qwe_job_state_is_final(jobs[i].state);
			live += jobs[i].state == QWE_JOB_RUNNING;
		}
		if (final == n)
			break;
		if (live == 0) {
			rc = -1; /* cannot happen: validation rejects cycles */
			break;
		}

		ne = epoll_wait(ctx->ep, got, 32, -1);
		for (i = 0; ne > 0 && i < (size_t)ne; i++) {
			struct ev *e = got[i].data.ptr;

			if (e->kind == EV_CHILD)
				reap_children(ctx, jobs, n);
			else if (e->kind == EV_CANCEL)
				poll_cancel(ctx);
			else
				job_event(ctx, e->job, e->kind);
		}
	}
	for (i = 0; i < n; i++)
		free(jobs[i].needs_idx);
	close(ctx->ep);
	free(sj);
	free(starts);
	return rc;
}

int qwe_run_workflow(const char *path)
{
	char run_id[64], *dir, *run_dir;
	lua_State *L;
	struct job *jobs = NULL;
	struct run_ctx ctx;
	sigset_t cancel_set;
	const char *grace_env;
	struct qwe_job_result *results;
	enum qwe_job_state *states;
	const char *slash;
	long n, max_parallel;
	size_t i;
	int rc = QWE_EXIT_OK;
	FILE *fp;

	if (load_workflow("qwe run", path, &L) != 0)
		return QWE_EXIT_USAGE;
	n = load_jobs(L, path, &jobs);
	if (n < 0)
		return QWE_EXIT_USAGE;

	/* .qwe/runs/<run-id>/ next to the workflow. */
	slash = strrchr(path, '/');
	dir = slash ? strndup(path, (size_t)(slash - path)) : strdup(".");
	fmt_run_id(run_id, sizeof run_id);
	run_dir = malloc(strlen(dir) + strlen(run_id) + 32);
	sprintf(run_dir, "%s/.qwe/runs/%s", dir, run_id);
	if (mkdir_p(run_dir) < 0) {
		fprintf(stderr, "qwe run: cannot create %s: %s\n", run_dir, strerror(errno));
		return QWE_EXIT_USAGE;
	}

	/* SIGCHLD, SIGINT and SIGTERM are read through signalfds, so they must be
	 * blocked. Children start with an empty mask (see proc.c). */
	sigemptyset(&ctx.chld_mask);
	sigaddset(&ctx.chld_mask, SIGCHLD);
	sigemptyset(&cancel_set);
	sigaddset(&cancel_set, SIGINT);
	sigaddset(&cancel_set, SIGTERM);
	sigprocmask(SIG_BLOCK, &ctx.chld_mask, NULL);
	sigprocmask(SIG_BLOCK, &cancel_set, NULL);
	ctx.cancel_fd = signalfd(-1, &cancel_set, SFD_CLOEXEC | SFD_NONBLOCK);
	ctx.chld_fd = signalfd(-1, &ctx.chld_mask, SFD_CLOEXEC | SFD_NONBLOCK);
	ctx.cancel_requested = 0;
	/* The grace period is fixed at 10 seconds; the override is for tests only. */
	grace_env = getenv("QWE_TEST_GRACE_MS");
	ctx.grace_ms = grace_env ? atol(grace_env) : 10000;

	lua_getfield(L, -1, "max-parallel");
	max_parallel = lua_isnumber(L, -1) ? (long)lua_tonumber(L, -1) : 0;
	lua_pop(L, 1);
	if (run_all(&ctx, L, jobs, (size_t)n, run_dir, max_parallel) < 0)
		rc = QWE_EXIT_FAILED;

	results = calloc((size_t)n, sizeof *results);
	states = calloc((size_t)n ? (size_t)n : 1, sizeof *states);
	for (i = 0; i < (size_t)n; i++) {
		results[i].id = jobs[i].id;
		results[i].outcome = qwe_job_state_name(jobs[i].state);
		results[i].reason = jobs[i].reason;
		results[i].started = jobs[i].started;
		results[i].ended = jobs[i].ended;
		results[i].dropped_bytes = jobs[i].dropped;
		results[i].steps = jobs[i].steps;
		results[i].nsteps = jobs[i].steps ? jobs[i].nsteps : 0;
		states[i] = jobs[i].state;
	}

	if (!qwe_jobs_all_ok(states, (size_t)n))
		rc = QWE_EXIT_FAILED;
	if (ctx.cancel_requested)
		rc = QWE_EXIT_CANCELLED; /* the operator's cancel outranks any job outcome */
	free(states);

	strcat(run_dir, "/result.json");
	fp = fopen(run_dir, "w");
	if (!fp || qwe_result_write(fp, run_id, results, (size_t)n) < 0 || fclose(fp) != 0) {
		fprintf(stderr, "qwe run: cannot write %s: %s\n", run_dir, strerror(errno));
		rc = QWE_EXIT_FAILED;
	}
	lua_close(L);
	free(results);
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
