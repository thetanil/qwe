#include "src/kernel/dag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long find(const struct qwe_dag_job *jobs, size_t n, const char *id)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (strcmp(jobs[i].id, id) == 0)
			return (long)i;
	return -1;
}

enum { WHITE, GREY, BLACK };

struct walk {
	const struct qwe_dag_job *jobs;
	size_t n;
	int *color;
	size_t *stack; /* jobs on the current path */
	size_t depth;
	struct qwe_dag_error *err;
};

/* Depth-first search; returns 1 as soon as a cycle is found. */
static int visit(struct walk *w, size_t j)
{
	size_t k;

	w->color[j] = GREY;
	w->stack[w->depth++] = j;
	for (k = 0; k < w->jobs[j].nneeds; k++) {
		size_t d = (size_t)find(w->jobs, w->n, w->jobs[j].needs[k]);

		if (w->color[d] == GREY) {
			/* d is on the stack: the cycle is the stack from d to here. */
			size_t s = 0, len = 0;
			struct qwe_dag_error *e = w->err;

			while (w->stack[s] != d)
				s++;
			w->err->status = QWE_DAG_CYCLE;
			w->err->job = d;
			len = (size_t)snprintf(e->message, sizeof e->message, "needs cycle: ");
			for (; s < w->depth && len < sizeof e->message; s++)
				len += (size_t)snprintf(e->message + len, sizeof e->message - len, "%s -> ",
							w->jobs[w->stack[s]].id);
			if (len < sizeof e->message)
				snprintf(e->message + len, sizeof e->message - len, "%s", w->jobs[d].id);
			return 1;
		}
		if (w->color[d] == WHITE && visit(w, d))
			return 1;
	}
	w->depth--;
	w->color[j] = BLACK;
	return 0;
}

enum qwe_dag_status qwe_dag_check(const struct qwe_dag_job *jobs, size_t n, struct qwe_dag_error *err)
{
	struct qwe_dag_error local;
	struct walk w;
	size_t i, k;
	int found = 0;

	if (!err)
		err = &local;
	memset(err, 0, sizeof *err);

	for (i = 0; i < n; i++) {
		for (k = 0; k < jobs[i].nneeds; k++) {
			if (find(jobs, n, jobs[i].needs[k]) < 0) {
				err->status = QWE_DAG_UNKNOWN_NEED;
				err->job = i;
				err->need = k;
				snprintf(err->message, sizeof err->message, "job \"%s\" needs \"%s\", which is not a job",
					 jobs[i].id, jobs[i].needs[k]);
				return err->status;
			}
		}
	}

	w.jobs = jobs;
	w.n = n;
	w.err = err;
	w.depth = 0;
	w.color = calloc(n ? n : 1, sizeof *w.color);
	w.stack = calloc(n ? n : 1, sizeof *w.stack);
	if (w.color && w.stack)
		for (i = 0; i < n && !found; i++)
			if (w.color[i] == WHITE)
				found = visit(&w, i);
	free(w.color);
	free(w.stack);
	return found ? err->status : QWE_DAG_OK;
}
