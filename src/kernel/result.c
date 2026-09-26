#define _POSIX_C_SOURCE 200809L
#include "src/kernel/result.h"

#include "src/kernel/put.h"

static void put_time(FILE *fp, time_t t)
{
	char buf[32];
	struct tm tm;

	if (t == 0) { /* never started */
		qwe_out_str(fp, "null");
		return;
	}

	/* A time gmtime cannot break down (a year past INT_MAX) has no ISO 8601
	 * form: write the epoch seconds, still a string, rather than an unset buffer. */
	if (!gmtime_r(&t, &tm) || strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
		qwe_out_fmt(fp, "\"%lld\"", (long long)t);
		return;
	}
	qwe_out_fmt(fp, "\"%s\"", buf);
}

static void put_opt(FILE *fp, const char *s)
{
	if (s)
		qwe_json_string(fp, s);
	else
		qwe_out_str(fp, "null");
}

static void put_duration(FILE *fp, long ms)
{
	if (ms < 0)
		qwe_out_str(fp, "null");
	else
		qwe_out_fmt(fp, "%ld", ms);
}

void qwe_json_string(FILE *fp, const char *s)
{
	qwe_out_ch(fp, '"');
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '"':
			qwe_out_str(fp, "\\\"");
			break;
		case '\\':
			qwe_out_str(fp, "\\\\");
			break;
		case '\n':
			qwe_out_str(fp, "\\n");
			break;
		case '\r':
			qwe_out_str(fp, "\\r");
			break;
		case '\t':
			qwe_out_str(fp, "\\t");
			break;
		default:
			if (c < 0x20)
				qwe_out_fmt(fp, "\\u%04x", c);
			else
				qwe_out_ch(fp, c);
		}
	}
	qwe_out_ch(fp, '"');
}

int qwe_result_write(FILE *fp, const char *run_id, const struct qwe_job_result *jobs, size_t njobs)
{
	size_t i, k;

	qwe_out_str(fp, "{\n  \"run_id\": ");
	qwe_json_string(fp, run_id);
	qwe_out_str(fp, ",\n  \"jobs\": {");
	for (i = 0; i < njobs; i++) {
		const struct qwe_job_result *j = &jobs[i];
		qwe_out_str(fp, i ? ",\n    " : "\n    ");
		qwe_json_string(fp, j->id);
		qwe_out_str(fp, ": {\n      \"outcome\": ");
		qwe_json_string(fp, j->outcome);
		qwe_out_str(fp, ",\n      \"reason\": ");
		put_opt(fp, j->reason);
		if (j->detail) {
			qwe_out_str(fp, ",\n      \"detail\": ");
			qwe_json_string(fp, j->detail);
		}
		qwe_out_str(fp, ",\n      \"started\": ");
		put_time(fp, j->started);
		qwe_out_str(fp, ",\n      \"ended\": ");
		put_time(fp, j->ended);
		qwe_out_str(fp, ",\n      \"duration_ms\": ");
		put_duration(fp, j->duration_ms);
		qwe_out_fmt(fp, ",\n      \"dropped_bytes\": %lu", j->dropped_bytes);
		qwe_out_str(fp, ",\n      \"steps\": [");
		for (k = 0; k < j->nsteps; k++) {
			const struct qwe_step_result *s = &j->steps[k];
			qwe_out_str(fp, k ? ",\n        {\n          \"id\": " : "\n        {\n          \"id\": ");
			put_opt(fp, s->id);
			qwe_out_str(fp, ",\n          \"outcome\": ");
			qwe_json_string(fp, s->outcome);
			qwe_out_str(fp, ",\n          \"reason\": ");
			put_opt(fp, s->reason);
			qwe_out_fmt(fp, ",\n          \"changed\": %s", s->changed ? "true" : "false");
			qwe_out_str(fp, ",\n          \"started\": ");
			put_time(fp, s->started);
			qwe_out_str(fp, ",\n          \"ended\": ");
			put_time(fp, s->ended);
			qwe_out_str(fp, ",\n          \"duration_ms\": ");
			put_duration(fp, s->duration_ms);
			if (s->outputs_json)
				qwe_out_fmt(fp, ",\n          \"outputs\": %s", s->outputs_json);
			qwe_out_str(fp, "\n        }");
		}
		qwe_out_str(fp, j->nsteps ? "\n      ]\n    }" : "]\n    }");
	}
	qwe_out_str(fp, njobs ? "\n  }\n}\n" : "}\n}\n");
	return ferror(fp) ? -1 : 0;
}
