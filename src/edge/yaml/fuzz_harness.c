#include "src/edge/yaml/fuzz_harness.h"

#include "cbor.h"
#include "src/edge/yaml/transcode.h"
#include "src/kernel/luacbor.h"
#include "src/kernel/luavm.h"
#include "src/kernel/validate.h"

#include <lauxlib.h>
#include <lua.h>
#include <stdio.h>
#include <stdlib.h>

static void dup_in_order(const char *pointer, struct qwe_pos first, struct qwe_pos second, void *ud)
{
	(void)pointer;
	(void)ud;
	if (second.line < first.line || (second.line == first.line && second.col <= first.col))
		abort();
}

static void check_cbor(const uint8_t *cbor, size_t n)
{
	CborParser parser;
	CborValue it;

	if (cbor_parser_init(cbor, n, 0, &parser, &it) != CborNoError || cbor_value_validate_basic(&it) != CborNoError)
		abort();
}

/* Returns 0 with *cbor and *pos for the caller to free, or -1. */
static int transcode(const uint8_t *data, size_t len, uint8_t **cbor, size_t *n, struct qwe_positions **pos)
{
	char err[256];

	if (qwe_yaml_to_cbor((const char *)data, len, cbor, n, pos, err, sizeof err) < 0)
		return -1;
	check_cbor(*cbor, *n);
	qwe_positions_duplicates(*pos, dup_in_order, NULL);
	return 0;
}

void qwe_fuzz_transcode(const uint8_t *data, size_t len)
{
	uint8_t *cbor;
	size_t n;
	struct qwe_positions *pos;

	if (transcode(data, len, &cbor, &n, &pos) < 0)
		return;
	free(cbor);
	qwe_positions_free(pos);
}

static lua_State *chain_L;

void qwe_fuzz_chain(const uint8_t *data, size_t len)
{
	lua_State *L = chain_L;
	uint8_t *cbor;
	size_t n;
	struct qwe_positions *pos;
	char err[256];
	int top;

	if (!L) {
		stderr = fopen("/dev/null", "w");
		L = chain_L = qwe_lua_new();
		if (!L || !stderr)
			abort();
	}
	if (transcode(data, len, &cbor, &n, &pos) < 0)
		return;
	top = lua_gettop(L);
	if (qwe_cbor_to_lua(L, cbor, n, err, sizeof err) == 0) {
		qwe_validate_doc(L, "fuzz", "/nonexistent/qwe-fuzz/w.yaml", pos);
		lua_settop(L, top);
	} else if (lua_gettop(L) != top) {
		abort();
	}
	free(cbor);
	qwe_positions_free(pos);
}

/* Not used by the fuzz binaries, which keep chain_L for the process's life on
 * purpose (recreating a Lua state per iteration would dominate fuzzing time).
 * corpus_test.c is a finite, one-shot replay, so it calls this once at the end
 * to close chain_L -- lua_close() runs every live userdata's __gc, including
 * rex_pcre2's, which frees the PCRE2 objects a `pattern:` keyword now
 * allocates (ticket 17); left open, they are indistinguishable from a leak to
 * LeakSanitizer, which cannot see into LuaJIT's own memory arena to find the
 * reachable path back to them through chain_L. */
void qwe_fuzz_chain_close(void)
{
	if (chain_L)
		lua_close(chain_L);
	chain_L = NULL;
}
