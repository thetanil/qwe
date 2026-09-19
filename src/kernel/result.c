#define _POSIX_C_SOURCE 200809L
#include "src/kernel/result.h"

static void put_time(FILE *fp, time_t t)
{
	char buf[32];
	struct tm tm;

	if (t == 0) { /* never started */
		fputs("null", fp);
		return;
	}

	gmtime_r(&t, &tm);
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
	fprintf(fp, "\"%s\"", buf);
}

static void put_opt(FILE *fp, const char *s)
{
	if (s)
		qwe_json_string(fp, s);
	else
		fputs("null", fp);
}

void qwe_json_string(FILE *fp, const char *s)
{
	fputc('"', fp);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		switch (c) {
		case '"':
			fputs("\\\"", fp);
			break;
		case '\\':
			fputs("\\\\", fp);
			break;
		case '\n':
			fputs("\\n", fp);
			break;
		case '\r':
			fputs("\\r", fp);
			break;
		case '\t':
			fputs("\\t", fp);
			break;
		default:
			if (c < 0x20)
				fprintf(fp, "\\u%04x", c);
			else
				fputc(c, fp);
		}
	}
	fputc('"', fp);
}

int qwe_result_write(FILE *fp, const char *run_id, const struct qwe_job_result *jobs, size_t njobs)
{
	size_t i, k;

	fputs("{\n  \"run_id\": ", fp);
	qwe_json_string(fp, run_id);
	fputs(",\n  \"jobs\": {", fp);
	for (i = 0; i < njobs; i++) {
		const struct qwe_job_result *j = &jobs[i];
		fputs(i ? ",\n    " : "\n    ", fp);
		qwe_json_string(fp, j->id);
		fputs(": {\n      \"outcome\": ", fp);
		qwe_json_string(fp, j->outcome);
		fputs(",\n      \"reason\": ", fp);
		put_opt(fp, j->reason);
		fputs(",\n      \"started\": ", fp);
		put_time(fp, j->started);
		fputs(",\n      \"ended\": ", fp);
		put_time(fp, j->ended);
		fprintf(fp, ",\n      \"dropped_bytes\": %lu", j->dropped_bytes);
		fputs(",\n      \"steps\": [", fp);
		for (k = 0; k < j->nsteps; k++) {
			const struct qwe_step_result *s = &j->steps[k];
			fputs(k ? ",\n        {\n          \"id\": " : "\n        {\n          \"id\": ", fp);
			put_opt(fp, s->id);
			fputs(",\n          \"outcome\": ", fp);
			qwe_json_string(fp, s->outcome);
			fputs(",\n          \"reason\": ", fp);
			put_opt(fp, s->reason);
			fprintf(fp, ",\n          \"changed\": %s", s->changed ? "true" : "false");
			fputs(",\n          \"started\": ", fp);
			put_time(fp, s->started);
			fputs(",\n          \"ended\": ", fp);
			put_time(fp, s->ended);
			fputs("\n        }", fp);
		}
		fputs(j->nsteps ? "\n      ]\n    }" : "]\n    }", fp);
	}
	fputs(njobs ? "\n  }\n}\n" : "}\n}\n", fp);
	return ferror(fp) ? -1 : 0;
}
