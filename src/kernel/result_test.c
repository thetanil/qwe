#define _POSIX_C_SOURCE 200809L
#include "greatest.h"
#include "src/kernel/result.h"

#include <stdlib.h>
#include <string.h>

/* Runs qwe_json_string on s and returns what it wrote (caller frees). */
static char *escaped(const char *s)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&buf, &len);

	qwe_json_string(fp, s);
	fclose(fp);
	return buf;
}

/* The expected literals are RFC 8259 §7 worked by hand: quote, backslash and
 * the control characters below U+0020 are escaped; everything else, UTF-8
 * included, passes through as it is. */
TEST escaping(void)
{
	char *out;

	out = escaped("plain");
	ASSERT_STR_EQ("\"plain\"", out);
	free(out);

	out = escaped("say \"hi\"");
	ASSERT_STR_EQ("\"say \\\"hi\\\"\"", out);
	free(out);

	out = escaped("a\\b");
	ASSERT_STR_EQ("\"a\\\\b\"", out);
	free(out);

	out = escaped("l1\nl2\r\tend");
	ASSERT_STR_EQ("\"l1\\nl2\\r\\tend\"", out);
	free(out);

	/* no short escape for these: \u00XX, lower-case hex */
	out = escaped("\x01\x1f\x0b");
	ASSERT_STR_EQ("\"\\u0001\\u001f\\u000b\"", out);
	free(out);

	/* the byte after the last control character, and DEL, are literal */
	out = escaped(" \x7f");
	ASSERT_STR_EQ("\" \x7f\"", out);
	free(out);

	out = escaped("caf\xc3\xa9");
	ASSERT_STR_EQ("\"caf\xc3\xa9\"", out);
	free(out);

	out = escaped("");
	ASSERT_STR_EQ("\"\"", out);
	free(out);
	PASS();
}

/* Ids, reasons and detail come from authored YAML: all of them go through the escaper. */
TEST hostile_text_in_a_result(void)
{
	struct qwe_step_result step = {
		.id = "s\"1", .outcome = "failed", .reason = "bad\nreason", .changed = 0, .outputs_json = NULL,
	};
	struct qwe_job_result job = {
		.id = "j\\1", .outcome = "cancelled", .reason = "r\"", .detail = "d\x01",
		.steps = &step, .nsteps = 1,
	};
	char *buf = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&buf, &len);

	ASSERT_EQ(0, qwe_result_write(fp, "run\"id", &job, 1));
	fclose(fp);
	ASSERT(strstr(buf, "\"run_id\": \"run\\\"id\""));
	ASSERT(strstr(buf, "\"j\\\\1\": {"));
	ASSERT(strstr(buf, "\"reason\": \"r\\\"\""));
	ASSERT(strstr(buf, "\"detail\": \"d\\u0001\""));
	ASSERT(strstr(buf, "\"id\": \"s\\\"1\""));
	ASSERT(strstr(buf, "\"reason\": \"bad\\nreason\""));
	/* no raw control character or newline inside any string: every raw
	 * newline is the writer's own layout, so none may follow a lone backslash */
	ASSERT_EQ(NULL, strstr(buf, "bad\nreason"));
	ASSERT_EQ(NULL, strchr(buf, '\x01'));
	free(buf);
	PASS();
}

/* duration_ms writes as a bare integer when set, and null when negative
 * (never started). */
TEST duration_written(void)
{
	struct qwe_step_result step = {
		.id = "s", .outcome = "success", .changed = 0, .duration_ms = 5,
	};
	struct qwe_step_result skipped_step = {
		.id = "t", .outcome = "skipped", .duration_ms = -1,
	};
	struct qwe_step_result steps[2];
	struct qwe_job_result job = {
		.id = "j", .outcome = "success", .duration_ms = 1234, .steps = steps, .nsteps = 2,
	};
	char *buf = NULL;
	size_t len = 0;
	FILE *fp = open_memstream(&buf, &len);

	steps[0] = step;
	steps[1] = skipped_step;
	ASSERT_EQ(0, qwe_result_write(fp, "run", &job, 1));
	fclose(fp);
	ASSERT(strstr(buf, "\"duration_ms\": 1234"));
	ASSERT(strstr(buf, "\"duration_ms\": 5"));
	ASSERT(strstr(buf, "\"duration_ms\": null"));
	free(buf);
	PASS();
}

SUITE(result)
{
	RUN_TEST(escaping);
	RUN_TEST(hostile_text_in_a_result);
	RUN_TEST(duration_written);
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_SUITE(result);
	GREATEST_MAIN_END();
}
