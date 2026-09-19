#define _GNU_SOURCE
#include "src/kernel/preamble.h"

#include <stdlib.h>
#include <string.h>

/* dash's `read` takes one byte at a time, so nothing past the empty line is
 * consumed. printf's %b undoes the escaping; the trailing "." keeps a value's
 * trailing newlines through the command substitution. */
const char qwe_preamble_bootstrap[] =
	"while IFS= read -r qwe_l; do\n"
	"  [ -n \"$qwe_l\" ] || break\n"
	"  qwe_v=$(printf '%b.' \"${qwe_l#*=}\") || exit 125\n"
	"  export \"${qwe_l%%=*}=${qwe_v%.}\"\n"
	"done\n"
	"unset qwe_l qwe_v\n"
	"exec sh -c \"$1\"\n";

static int name_ok(const char *s)
{
	if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_'))
		return 0;
	for (s++; *s; s++)
		if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_'))
			return 0;
	return 1;
}

int qwe_preamble_build(const char *const *names, const char *const *values, size_t n, char **out, size_t *len,
		       size_t *bad)
{
	size_t i, cap = 1, l = 0;
	char *buf, *o;
	const char *s;

	for (i = 0; i < n; i++) {
		if (!name_ok(names[i])) {
			*bad = i;
			return -1;
		}
		cap += strlen(names[i]) + 2;
		for (s = values[i]; *s; s++)
			cap += (*s == '\\' || *s == '\n') ? 2 : 1;
	}
	buf = malloc(cap + 1);
	if (!buf)
		return -1;
	o = buf;
	for (i = 0; i < n; i++) {
		o = stpcpy(o, names[i]);
		*o++ = '=';
		for (s = values[i]; *s; s++) {
			if (*s == '\\') {
				*o++ = '\\';
				*o++ = '\\';
			} else if (*s == '\n') {
				*o++ = '\\';
				*o++ = 'n';
			} else {
				*o++ = *s;
			}
		}
		*o++ = '\n';
	}
	*o++ = '\n';
	l = (size_t)(o - buf);
	*out = buf;
	*len = l;
	return 0;
}
