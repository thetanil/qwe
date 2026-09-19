#define _GNU_SOURCE
#include "src/kernel/qwe.h"

#include "src/edge/yaml/transcode.h"
#include "src/kernel/jobs.h"
#include "src/kernel/lifecycle.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"
#include "src/kernel/validate.h"
#include "src/kernel/proc.h"
#include "src/kernel/timer.h"
#include "src/kernel/trace.h"
#include "src/kernel/result.h"
#include "src/kernel/ring.h"
#include "src/kernel/sched.h"
#include "src/kernel/sink.h"

#include <errno.h>
#include <stdint.h>
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

/* What an epoll event is about. Every fd in the loop carries a tag: which job,
 * what kind, and the step it was opened for. The step is what lets a late
 * event from a step that has ended be told from an event of the live one. */
enum evkind { EV_OUT, EV_STEP_TIMER, EV_JOB_TIMER, EV_GRACE_TIMER, EV_CHILD, EV_CANCEL };

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
	id = lua_isstring(L, -1) ? strdup(lua_tostring(L, -1)) : NULL;
	lua_pop(L, 4);
	return id;
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
		r->ring_ok = 0;
	}
}

/* Action: open the job's log, ring and timer, and arm its timeout. */
static void job_start(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;
	const char *op = NULL;
	int err = 0;

	job->L = ctx->L;
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
	job->steps = calloc(job->nsteps ? job->nsteps : 1, sizeof *job->steps);
	if (job->timeout_ms > 0)
		qwe_timer_arm(&r->timer, job->timeout_ms);
	ev_add(ctx, job, EV_JOB_TIMER, r->timer.fd, -1);
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
	lua_rawgeti(L, LUA_REGISTRYINDEX, job->ref);
	lua_getfield(L, -1, "steps");
	lua_rawgeti(L, -1, (int)r->cur + 1);
	step_ms = qwe_timeout_ms_at(L, -1);
	r->cur_ref = luaL_ref(L, LUA_REGISTRYINDEX); /* pops the step table */
	lua_pop(L, 2);

	res->started = time(NULL);
	r->live_step = (long)r->cur;
	r->leader_reaped = 0;
	arg.L = L;
	arg.step_ref = r->cur_ref;
	if (qwe_timer_open(&r->step_timer) < 0) {
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
	ev_add(ctx, job, EV_STEP_TIMER, r->step_timer.fd, r->live_step);
	ev_add(ctx, job, EV_GRACE_TIMER, r->grace_timer.fd, r->live_step);
	r->proc_ok = 1;
}

/* The step's group is empty (or it never started): release what it held and
 * move the cursor past it. */
static void step_finish(struct run_ctx *ctx, struct job *job)
{
	struct job_run *r = &job->run;

	if (r->live_step < 0)
		return;
	if (r->proc_ok) {
		/* The child is gone; whatever it wrote is already in the pipe. */
		drain(r->proc.out_fd, &r->ring, &r->sink);
		ev_del(ctx, r->proc.out_fd);
		close(r->proc.out_fd);
		ev_del(ctx, r->step_timer.fd);
		ev_del(ctx, r->grace_timer.fd);
		qwe_timer_close(&r->step_timer);
		qwe_timer_close(&r->grace_timer);
		r->proc_ok = 0;
	}
	luaL_unref(job->L, LUA_REGISTRYINDEX, r->cur_ref);
	r->cur++;
	r->live_step = -1;
}

/* Action: how the live step ended, as the table decided. */
static void record_step(struct job *job, const struct qwe_lc_result *res)
{
	struct qwe_step_result *s = &job->steps[job->run.cur];

	s->ended = time(NULL);
	s->changed = 1; /* run: steps always count as changed */
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
		for (i = r->cur; i < job->nsteps; i++) {
			job->steps[i].id = step_id(job, i);
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
		qwe_proc_kill_group(&r->proc, SIGTERM);
		break;
	case QWE_LC_ACT_ARM_GRACE:
		qwe_timer_arm(&r->grace_timer, ctx->grace_ms);
		break;
	case QWE_LC_ACT_SIGKILL_GROUP:
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
			job_send(ctx, job,
				 WIFEXITED(status) && WEXITSTATUS(status) == 0 ? QWE_LC_EV_LEADER_EXIT_OK
									       : QWE_LC_EV_LEADER_EXIT_FAIL,
				 pl, r->live_step);
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
		if (live && drain(r->proc.out_fd, &r->ring, &r->sink))
			ev_del(ctx, r->proc.out_fd);
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

/* Runs every job to a final state on one event loop. The loop turns fds and
 * signals into events, and the lifecycle table decides what each one means. A
 * job whose needs are not all success is skipped (the default join rule); at
 * most max_parallel jobs run at once, started in id order. An operator cancel
 * is sent to every job once. */
static int run_all(struct run_ctx *ctx, long max_parallel)
{
	struct job *jobs = ctx->jobs;
	size_t n = ctx->njobs;
	struct qwe_sched_job *sj = calloc(n ? n : 1, sizeof *sj);
	struct qwe_sched_event *evs = calloc(n ? n : 1, sizeof *evs);
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
		jobs[i].needs_idx = calloc(jobs[i].nneeds ? jobs[i].nneeds : 1, sizeof(size_t));
		for (k = 0; k < jobs[i].nneeds; k++)
			jobs[i].needs_idx[k] = (size_t)(find_job(jobs, n, jobs[i].needs[k]) - jobs);
		sj[i].state = &jobs[i].state;
		sj[i].needs = jobs[i].needs_idx;
		sj[i].nneeds = jobs[i].nneeds;
	}

	for (;;) {
		size_t running = 0, final = 0, got_n;
		int ne;

		/* A job started by the pass can read the cancel off its signalfd
		 * (between_steps_event), so the broadcast goes again after it:
		 * otherwise the other jobs would not hear it until the next wakeup. */
		broadcast_cancel(ctx);
		do {
			while ((got_n = qwe_sched_pass(sj, n, max_parallel, evs)) > 0)
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
			rc = -1; /* cannot happen: validation rejects cycles */
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
	free(sj);
	free(evs);
	return rc;
}

int qwe_run_workflow(const char *path, const struct qwe_run_options *opts)
{
	char run_id[64], *dir, *run_dir, *trace_path;
	lua_State *L;
	struct job *jobs = NULL;
	struct run_ctx ctx;
	sigset_t cancel_set;
	const char *grace_env;
	struct qwe_job_result *results;
	const char *slash;
	long n, max_parallel;
	size_t i;
	int rc = QWE_EXIT_OK, all_ok = 1;
	FILE *fp;

	if (load_workflow("qwe run", path, &L) != 0)
		return QWE_EXIT_USAGE;
	n = qwe_jobs_load(L, path, &jobs);
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
	trace_path = malloc(strlen(run_dir) + 32);
	sprintf(trace_path, "%s/lifecycle.trace", run_dir);
	memset(&ctx, 0, sizeof ctx);
	if (qwe_trace_open(&ctx.trace, trace_path, opts && opts->debug) < 0) {
		fprintf(stderr, "qwe run: cannot create %s: %s\n", trace_path, strerror(errno));
		return QWE_EXIT_USAGE;
	}
	free(trace_path);
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
	ctx.L = L;

	lua_getfield(L, -1, "max-parallel");
	max_parallel = lua_isnumber(L, -1) ? (long)lua_tonumber(L, -1) : 0;
	lua_pop(L, 1);
	if (run_all(&ctx, max_parallel) < 0)
		rc = QWE_EXIT_FAILED;

	results = calloc((size_t)n ? (size_t)n : 1, sizeof *results);
	for (i = 0; i < (size_t)n; i++) {
		results[i].id = jobs[i].id;
		results[i].outcome = qwe_lc_state_name(jobs[i].state);
		results[i].reason = jobs[i].reason;
		results[i].started = jobs[i].started;
		results[i].ended = jobs[i].ended;
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
	qwe_lc_set_abort_hook(NULL, NULL);
	qwe_trace_close(&ctx.trace);
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
