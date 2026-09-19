/* The jobs of a workflow as the engine runs them: what is read from the
 * decoded workflow, and what a running job holds. */
#ifndef QWE_KERNEL_JOBS_H
#define QWE_KERNEL_JOBS_H

#include "src/kernel/lifecycle.h"
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
	struct qwe_timer timer;      /* the job's own limit */
	struct qwe_timer step_timer; /* the live step's limit */
	struct qwe_timer grace_timer;
	struct qwe_proc proc;
	int ring_ok, sink_ok, timer_ok; /* which of the job's resources are held */
	int proc_ok;                 /* the live step's pipe and timers are open */
	int leader_reaped;           /* the live step's leader has been waited for */
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

/* Milliseconds for the timeout-minutes of the table at idx; 0 (no limit) if it is absent. */
long qwe_timeout_ms_at(lua_State *L, int idx);

/* Reads the jobs out of the decoded workflow (on top of L's stack) into a
 * list sorted by id, and refuses what the engine cannot run yet. Returns the
 * job count, or -1 after printing why. */
long qwe_jobs_load(lua_State *L, const char *path, struct job **out);

#endif
