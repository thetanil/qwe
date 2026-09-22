#include "src/kernel/summary.h"

#include <string.h>

/* Outcomes are text first, so the table also reads as plain text without a renderer. */
static const char *marker(const char *outcome)
{
	if (strcmp(outcome, "success") == 0)
		return "\xE2\x9C\x85 success"; /* U+2705 */
	if (strcmp(outcome, "failed") == 0)
		return "\xE2\x9D\x8C failed"; /* U+274C */
	if (strcmp(outcome, "skipped") == 0)
		return "\xE2\x8F\xAD\xEF\xB8\x8F skipped"; /* U+23ED U+FE0F */
	if (strcmp(outcome, "cancelled") == 0)
		return "\xE2\x8F\xB9\xEF\xB8\x8F cancelled"; /* U+23F9 U+FE0F */
	return outcome;
}

static void put_ms(FILE *fp, long ms)
{
	if (ms >= 0)
		fprintf(fp, "%ld", ms);
}

/* "changed" for a success that changed something, "unchanged" for a success that
 * did not (its check found nothing to do), blank otherwise: skipped is an outcome
 * in its own right, never mixed up with unchanged. */
static const char *changed_cell(const struct qwe_step_result *s)
{
	if (strcmp(s->outcome, "success") != 0)
		return "";
	return s->changed ? "changed" : "unchanged";
}

int qwe_summary_write(const char *path, const char *workflow_file, const char *overall_outcome,
		      long run_duration_ms, const struct qwe_job_result *jobs, size_t njobs)
{
	FILE *fp = fopen(path, "a");
	size_t i, k;

	if (!fp)
		return -1;

	fprintf(fp, "### %s: %s (", workflow_file, marker(overall_outcome));
	put_ms(fp, run_duration_ms);
	fputs(" ms)\n\n", fp);

	fputs("| job | outcome | reason | duration (ms) |\n", fp);
	fputs("| --- | --- | --- | --- |\n", fp);
	for (i = 0; i < njobs; i++) {
		const struct qwe_job_result *j = &jobs[i];

		fprintf(fp, "| %s | %s | %s | ", j->id, marker(j->outcome), j->reason ? j->reason : "");
		put_ms(fp, j->duration_ms);
		fputs(" |\n", fp);
	}
	fputc('\n', fp);

	for (i = 0; i < njobs; i++) {
		const struct qwe_job_result *j = &jobs[i];

		if (!j->nsteps)
			continue;
		fprintf(fp, "#### %s\n\n", j->id);
		fputs("| # | id | plugin | outcome | changed | reason | duration (ms) |\n", fp);
		fputs("| --- | --- | --- | --- | --- | --- | --- |\n", fp);
		for (k = 0; k < j->nsteps; k++) {
			const struct qwe_step_result *s = &j->steps[k];
			const char *label = s->id ? s->id : (s->name ? s->name : "");

			fprintf(fp, "| %lu | %s | %s | %s | %s | %s | ", (unsigned long)(k + 1), label,
				s->plugin ? s->plugin : "run", marker(s->outcome), changed_cell(s),
				s->reason ? s->reason : "");
			put_ms(fp, s->duration_ms);
			fputs(" |\n", fp);
		}
		fputc('\n', fp);
	}

	if (fclose(fp) != 0)
		return -1;
	return 0;
}
