/* The bare allocation calls that neither `qwe run` nor `qwe validate` reaches
 * with a workflow of the usual shape (ticket quality/08): the redactor, the
 * CBOR map encoder and qwe.exec.run. Each is called on its own with the nth
 * allocation failed, for every n it makes. A call that hit the failure must
 * report it (that is the whole assertion: a fault, a hang or a silently short
 * result fails), and one that did not must have produced the right result. */
#include "greatest.h"
#include "src/kernel/luavm.h"
#include "src/kernel/oom_shim.h"
#include "src/kernel/redact.h"

#include <lauxlib.h>
#include <stdio.h>
#include <string.h>

/* Case codes: 0 the call failed cleanly, 1 it worked, 2 it lied or did not say why. */
static int redact_case(void *arg)
{
	struct qwe_redactor r = {0};
	struct qwe_redact_buf out = {0};

	(void)arg;
	if (qwe_redact_feed(&r, "aa hunter2 bb hunt", 18, &out) < 0 || qwe_redact_flush(&r, &out) < 0)
		return 0;
	return out.len == 14 && memcmp(out.data, "aa *** bb hunt", 14) == 0 ? 1 : 2;
}

/* Runs a chunk that returns "ok", or fails with a message that says memory. */
static int lua_case(void *arg)
{
	lua_State *L = arg;
	const char *res;

	lua_pcall(L, 0, 1, 0); /* the result, or the error message: either is on top */
	res = lua_tostring(L, -1);
	if (res && strcmp(res, "ok") == 0)
		return 1;
	if (res && strstr(res, "memory"))
		return 0;
	fprintf(stderr, "%s\n", res ? res : "(no message)");
	return 2;
}

static enum greatest_test_res sweep(int (*fn)(void *), void *arg)
{
	struct qwe_oom_outcome o;
	long at, count;

	ASSERT_EQ(0, qwe_oom_probe(1L << 40, 0, fn, arg, &o));
	ASSERT(o.exited);
	ASSERT_EQ_FMT(1, o.code, "%d");
	count = o.count;
	ASSERT(count > 0);
	for (at = 1; at <= count; at++) {
		ASSERT_EQ(0, qwe_oom_probe(at, 0, fn, arg, &o));
		if (!o.exited || o.code != 0 || !o.fired) {
			fprintf(stderr, "allocation %ld of %ld: exited %d code %d signal %d fired %d\n%s\n", at, count,
			    o.exited, o.code, o.signal, o.fired, o.err);
			FAIL();
		}
	}
	PASS();
}

TEST redactor_survives_every_injection(void)
{
	qwe_redact_add("hunter2", 7);
	CHECK_CALL(sweep(redact_case, NULL));
	qwe_redact_clear();
	PASS();
}

static enum greatest_test_res lua_sweep(const char *chunk)
{
	lua_State *L = qwe_lua_new();
	enum greatest_test_res res;

	ASSERT(L != NULL);
	ASSERT_EQ(0, luaL_loadstring(L, chunk));
	res = sweep(lua_case, L);
	lua_close(L);
	return res;
}

TEST cbor_map_survives_every_injection(void)
{
	/* more than the encoder's first eight keys, so its key list grows */
	return lua_sweep(
	    "local cbor = require('qwe.cbor')\n"
	    "local t = {}\n"
	    "for i = 1, 20 do t['k' .. i] = i end\n"
	    "local ok, s, e = pcall(cbor.encode, t)\n"
	    "if ok and type(s) == 'string' and #s > 20 then return 'ok' end\n"
	    "return 'fail: ' .. tostring(e or s)\n");
}

TEST exec_run_survives_every_injection(void)
{
	return lua_sweep(
	    "local exec = require('qwe.exec')\n"
	    "local ok, code, out = pcall(exec.run, { 'echo', 'hi' })\n"
	    "if ok and code == 0 and out == 'hi\\n' then return 'ok' end\n"
	    "return 'fail: ' .. tostring(ok and out or code)\n");
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();
	RUN_TEST(redactor_survives_every_injection);
	RUN_TEST(cbor_map_survives_every_injection);
	RUN_TEST(exec_run_survives_every_injection);
	GREATEST_MAIN_END();
}
