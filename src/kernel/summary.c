#define _GNU_SOURCE
#include "src/kernel/summary.h"

#include "src/kernel/alloc.h"

#include <stdlib.h>
#include <string.h>

#define CELL_MAX 200
#define LOG_TAIL_LINES 20

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

/* Escapes | and folds embedded newlines to a space, then cuts to CELL_MAX bytes
 * with an ellipsis if the escaped text is longer. Never NULL; caller frees. */
static char *escape_cell(const char *s)
{
	size_t len = strlen(s), i, out_len = 0;
	char *out = qwe_xmalloc(len * 2 + 1);

	for (i = 0; i < len; i++) {
		char c = s[i];

		if (c == '|') {
			out[out_len++] = '\\';
			out[out_len++] = '|';
		} else if (c == '\n') {
			out[out_len++] = ' ';
		} else {
			out[out_len++] = c;
		}
	}
	out[out_len] = '\0';
	if (out_len > CELL_MAX) {
		char *cut = qwe_xmalloc(CELL_MAX + 4); /* + "\xE2\x80\xA6" (3 bytes) + NUL */

		memcpy(cut, out, CELL_MAX);
		memcpy(cut + CELL_MAX, "\xE2\x80\xA6", 3);
		cut[CELL_MAX + 3] = '\0';
		free(out);
		out = cut;
	}
	return out;
}

static void put_cell(FILE *fp, const char *s)
{
	char *esc = escape_cell(s ? s : "");

	fputs(esc, fp);
	free(esc);
}

/* The longest run of consecutive backticks in s, so a fence can outrun it. */
static int longest_backtick_run(const char *s)
{
	int longest = 0, cur = 0;

	for (; *s; s++) {
		if (*s == '`') {
			if (++cur > longest)
				longest = cur;
		} else {
			cur = 0;
		}
	}
	return longest;
}

/* The last n lines of <run_dir>/<job_id>.log (already redacted), as a malloc'd,
 * NUL-terminated string ending in a newline, or NULL if it cannot be read.
 * A qwe job log always ends with a newline (the sink flushes one on close), so
 * every "\n" found while walking back is a line boundary. */
static char *log_tail(const char *run_dir, const char *job_id, int n)
{
	char *path, *buf, *start, *end;
	FILE *fp;
	long len;
	int left = n;

	if (asprintf(&path, "%s/%s.log", run_dir, job_id) < 0)
		return NULL;
	fp = fopen(path, "r");
	free(path);
	if (!fp)
		return NULL;
	/* fp is read-only throughout: a failed fclose loses nothing. */
	len = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
	/* not rewind(): it has no return, so a failure would go unseen */
	if (len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		(void)fclose(fp);
		return NULL;
	}
	buf = malloc((size_t)len + 1);
	if (!buf) {
		(void)fclose(fp);
		return NULL;
	}
	if (len > 0 && fread(buf, 1, (size_t)len, fp) != (size_t)len) {
		free(buf);
		(void)fclose(fp);
		return NULL;
	}
	(void)fclose(fp);
	buf[len] = '\0';

	end = buf + len;
	start = end;
	if (start > buf && start[-1] == '\n')
		start--; /* the last line's own terminator: not a boundary to count */
	while (start > buf && left > 0) {
		start--;
		if (*start == '\n' && --left == 0) {
			start++;
			break;
		}
	}
	if (start > buf)
		memmove(buf, start, (size_t)(end - start) + 1);
	return buf;
}

int qwe_summary_write(const char *path, const char *run_dir, const char *workflow_file,
		      const char *overall_outcome, long run_duration_ms,
		      const struct qwe_job_result *jobs, size_t njobs)
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

		fputs("| ", fp);
		put_cell(fp, j->id);
		fprintf(fp, " | %s | ", marker(j->outcome));
		put_cell(fp, j->reason ? j->reason : "");
		fputs(" | ", fp);
		put_ms(fp, j->duration_ms);
		fputs(" |\n", fp);
	}
	fputc('\n', fp);

	for (i = 0; i < njobs; i++) {
		const struct qwe_job_result *j = &jobs[i];
		int has_failure = 0;

		if (!j->nsteps)
			continue;
		fputs("#### ", fp);
		put_cell(fp, j->id);
		fputs("\n\n", fp);
		fputs("| # | id | plugin | outcome | changed | reason | duration (ms) |\n", fp);
		fputs("| --- | --- | --- | --- | --- | --- | --- |\n", fp);
		for (k = 0; k < j->nsteps; k++) {
			const struct qwe_step_result *s = &j->steps[k];
			const char *label = s->id ? s->id : (s->name ? s->name : "");

			fprintf(fp, "| %lu | ", (unsigned long)(k + 1));
			put_cell(fp, label);
			fputs(" | ", fp);
			put_cell(fp, s->plugin ? s->plugin : "run");
			fprintf(fp, " | %s | %s | ", marker(s->outcome), changed_cell(s));
			put_cell(fp, s->reason ? s->reason : "");
			fputs(" | ", fp);
			put_ms(fp, s->duration_ms);
			fputs(" |\n", fp);
			if (strcmp(s->outcome, "failed") == 0 || strcmp(s->outcome, "cancelled") == 0)
				has_failure = 1;
		}
		fputc('\n', fp);

		if (has_failure && run_dir) {
			char *tail = log_tail(run_dir, j->id, LOG_TAIL_LINES);

			if (tail && *tail) {
				int fence_len = longest_backtick_run(tail) + 1;
				int f;

				if (fence_len < 3)
					fence_len = 3;
				fputs("<details><summary>log tail</summary>\n\n", fp);
				for (f = 0; f < fence_len; f++)
					fputc('`', fp);
				fputc('\n', fp);
				fputs(tail, fp);
				if (tail[strlen(tail) - 1] != '\n')
					fputc('\n', fp);
				for (f = 0; f < fence_len; f++)
					fputc('`', fp);
				fputs("\n\n</details>\n\n", fp);
			}
			free(tail);
		}
	}

	if (fclose(fp) != 0)
		return -1;
	return 0;
}
