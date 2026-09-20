/* The jobs of a workflow as the engine runs them: what is read from the
 * decoded workflow, and what a running job holds. */
#ifndef QWE_KERNEL_JOBS_H
#define QWE_KERNEL_JOBS_H

#include "src/kernel/lifecycle.h"
#include "src/kernel/redact.h"
#include "src/kernel/proc.h"
#include "src/kernel/result.h"
#include "src/kernel/ring.h"
#include "src/kernel/sink.h"
#include "src/kernel/timer.h"

#include <lua.h>
#include <stddef.h>
#include <time.h>

/* What one running job holds beyond its result. */
struct job_run {
	struct qwe_ring ring;
	struct qwe_sink sink;
	struct qwe_redactor red;     /* between the step's pipe and the ring */
	struct qwe_timer timer;      /* the job's own limit */
	struct qwe_timer step_timer; /* the live step's limit */
	struct qwe_timer grace_timer;
	struct qwe_proc proc;
	int ring_ok, sink_ok, timer_ok; /* which of the job's resources are held */
	int proc_ok;                 /* the live step's pipe and timers are open */
	int leader_reaped;           /* the live step's leader has been waited for */
	char *res_buf;               /* what the live step has sent on its result pipe */
	size_t res_len, res_cap;
	int res_overflow;            /* it sent more than the engine keeps */
	int step_changed;            /* whether the live step changed the target */
	int outputs_ref;             /* registry ref of the job's { step id -> { key -> value } } */
	char *out_path;              /* the live run: step's $QWE_OUTPUT file */
	char *step_target;           /* the live step runs on this remote target, or NULL */
	int step_unreachable;        /* the live step was spawned with its target given up on */
	char *step_token;            /* what tells its processes apart on the remote host */
	char *step_json;             /* the live step's outputs, as JSON, for result.json */
	int timeout_told;            /* the job timeout has been sent as an event */
	int cancel_told;             /* the operator's cancel has been sent as an event */
	size_t cur;                  /* the step in flight, or the next to run */
	long live_step;              /* the step whose group is in flight, or -1 */
	int cur_ref;                 /* registry ref of the live step's table */
	const char *last_reason;     /* the last reason a transition recorded */
	/* an event the shell owes the job after an action failed */
	int have_followup;
	enum qwe_lc_event followup;
	const char *fail_op;         /* what could not be started, for the trace */
	int fail_errno;
};

struct job {
	char *id;
	char *target; /* the job's target: local, or an inventory target */
	int ref; /* registry ref of the job's Lua table */
	enum qwe_lc_state state;
	const char *reason;
	char **needs;
	size_t nneeds;
	struct qwe_step_result *steps; /* NULL until the job has started */
	size_t nsteps;
	time_t started, ended;
	unsigned long dropped;
	long timeout_ms; /* the job's own limit, 0 for none */
	lua_State *L;
	struct job_run run;
	size_t *needs_idx;
};

/* The largest timeout-seconds the schema admits (seven days). */
#define QWE_TIMEOUT_MAX_SECONDS 604800.0

/* Milliseconds for the timeout-seconds of the table at idx; 0 (no limit) if it is absent.
 * A positive value is at least 1 ms, and one above the maximum is clamped to it, so the
 * conversion never overflows and never turns a real limit into "none". */
long qwe_timeout_ms_at(lua_State *L, int idx);

/* Reads the jobs out of the decoded workflow (on top of L's stack) into a
 * list sorted by id, and refuses what the engine cannot run yet. Returns the
 * job count, or -1 after printing why. */
long qwe_jobs_load(lua_State *L, const char *path, struct job **out);

#endif
