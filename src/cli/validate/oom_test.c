/* qwe validate under memory pressure: the nth allocation fails, for every n
 * the run makes. Each run ends in a verdict or in a clean failure that names
 * the file, and never in a fault or a hang. The assertion is about the
 * outcome, not just survival: a workflow that is invalid must never be
 * reported valid because a check could not run (m1-review/07 was exactly
 * that), and one that is valid must not come back with a lesser verdict. */
#include "greatest.h"
#include "src/kernel/fmt.h"
#include "src/kernel/put.h"
#include "src/cli/validate/validate.h"
#include "src/kernel/oom_shim.h"
#include "src/kernel/qwe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char dir[512];

static void write_file(const char *name, const char *body)
{
	char path[700];
	FILE *fp;

	qwe_xfmt(path, sizeof path, "%s/%s", dir, name);
	fp = fopen(path, "w");
	if (!fp)
		abort();
	qwe_out_str(fp, body);
	if (ferror(fp) || fclose(fp) != 0)
		abort(); /* the fixture was not fully written */
}

static void make_dir(const char *rel)
{
	char path[700];

	qwe_xfmt(path, sizeof path, "%s/%s", dir, rel);
	mkdir(path, 0700);
}

struct scenario {
	const char *file;
	int expect; /* the exit code with no injection */
};

static int validate(void *arg)
{
	const struct scenario *sc = arg;
	char path[700], inv[700];
	char *argv[5] = {"validate", path, "-i", inv, NULL};

	qwe_xfmt(path, sizeof path, "%s/%s", dir, sc->file);
	qwe_xfmt(inv, sizeof inv, "%s/inventory.yaml", dir);
	return qwe_cmd_validate(4, argv);
}

/* Fails allocation 1, 2, 3 ... until one is past the last the run makes. */
static enum greatest_test_res sweep(const struct scenario *sc)
{
	struct qwe_oom_outcome base, o;
	long at;

	ASSERT_EQ(0, qwe_oom_probe(0, 0, validate, (void *)sc, &base));
	ASSERT(base.exited);
	ASSERT_EQ_FMT(sc->expect, base.code, "%d");
	ASSERT(base.count == 0); /* not armed: nothing counted */

	/* The first injected run is armed past the end, to learn how many allocations there are. */
	ASSERT_EQ(0, qwe_oom_probe(1L << 40, 0, validate, (void *)sc, &o));
	ASSERT(o.count > 10);
	ASSERT_EQ_FMT(sc->expect, o.code, "%d");
	for (at = 1; at <= o.count; at++) {
		struct qwe_oom_outcome r;

		ASSERT_EQ(0, qwe_oom_probe(at, 0, validate, (void *)sc, &r));
		if (!r.exited || (r.code != 0 && r.code != 2) || !r.fired) {
			qwe_diag("allocation %ld of %ld: exited %d code %d signal %d fired %d\n%s\n",
			    at, o.count, r.exited, r.code, r.signal, r.fired, r.err);
			FAIL();
		}
		if (sc->expect == 0 && r.code == 0) {
			/* a success despite the failure must be a whole one */
			if (r.err[0]) {
				qwe_diag("allocation %ld: succeeded but wrote:\n%s\n", at, r.err);
				FAIL();
			}
		} else if (sc->expect != 0 && r.code == 0) {
			qwe_diag("allocation %ld: an invalid workflow was reported valid\n", at);
			FAIL();
		}
		if (r.code != 0 && !strstr(r.err, dir)) {
			qwe_diag("allocation %ld: failure names no file (expected one under %s):\n%s\n", at, dir, r.err);
			FAIL();
		}
		if (r.code != 0 && sc->expect == 0 && !strstr(r.err, "memory")) {
			qwe_diag("allocation %ld: failure does not say memory:\n%s\n", at, r.err);
			FAIL();
		}
	}
	PASS();
}

TEST validate_survives_every_injection(void)
{
	static const struct scenario valid = {"w.yaml", 0};
	static const struct scenario cycle = {"cycle.yaml", 2};
	static const struct scenario dup = {"dup.yaml", 2};

	CHECK_CALL(sweep(&valid));
	CHECK_CALL(sweep(&cycle));
	CHECK_CALL(sweep(&dup));
	PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	const char *tmp = getenv("TEST_TMPDIR");

	qwe_xfmt(dir, sizeof dir, "%s", tmp ? tmp : "/tmp");
	make_dir(".qwe");
	make_dir(".qwe/plugins");
	make_dir(".qwe/plugins/hello");
	write_file(".qwe/plugins/hello/schema.json",
	    "{\"with\": {\"$schema\": \"http://json-schema.org/draft-07/schema#\", \"type\": \"object\",\n"
	    "  \"required\": [\"name\"], \"additionalProperties\": false,\n"
	    "  \"properties\": {\"name\": {\"type\": \"string\", \"minLength\": 1}}}}\n");
	write_file(".qwe/plugins/hello/plugin.lua",
	    "local M = {}\nfunction M.check(_with) return true end\n"
	    "function M.apply(with) print(with.name) end\nreturn M\n");
	write_file("inventory.yaml", "targets:\n  box:\n    backend: ssh\n    host: 192.0.2.1\n");
	write_file("w.yaml",
	    "jobs:\n"
	    "  build:\n    target: box\n    steps:\n      - run: echo hi\n"
	    "      - uses: hello\n        with:\n          name: world\n"
	    "  test:\n    target: local\n    needs: [build]\n    steps:\n      - run: echo hi\n");
	write_file("cycle.yaml",
	    "jobs:\n"
	    "  a:\n    target: local\n    needs: [b]\n    steps:\n      - run: echo hi\n"
	    "  b:\n    target: local\n    needs: [a]\n    steps:\n      - run: echo hi\n");
	write_file("dup.yaml",
	    "jobs:\n"
	    "  a:\n    target: local\n    steps:\n      - run: echo hi\n"
	    "  a:\n    target: local\n    steps:\n      - run: echo hi\n");
	GREATEST_MAIN_BEGIN();
	RUN_TEST(validate_survives_every_injection);
	GREATEST_MAIN_END();
}
