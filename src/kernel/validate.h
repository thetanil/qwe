/* Workflow validation: schema (in Lua) plus structural checks, with every
 * error reported as file:line:col. */
#ifndef QWE_KERNEL_VALIDATE_H
#define QWE_KERNEL_VALIDATE_H

#include "src/edge/yaml/positions.h"

#include <lua.h>

/* Validates the decoded workflow on top of L's stack. Prints each error to
 * stderr as "<cmd>: <path>:<line>:<col>: <message>", in source order, and
 * returns how many there were. The stack is left as it was. */
int qwe_validate_doc(lua_State *L, const char *cmd, const char *path, const struct qwe_positions *pos);

/* The same for the decoded inventory on top of L's stack (qwe.inventory). */
int qwe_validate_inventory(lua_State *L, const char *cmd, const char *path, const struct qwe_positions *pos);

#endif
