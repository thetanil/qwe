/* qwe.secrets: what Lua may do with secrets, all of it in the parent.
 *   reveal(secret)  decrypts an !encrypted value (a secret table from qwe.cbor)
 *                   with the run key and adds the plaintext to the run's
 *                   redaction set. Raises an error naming what went wrong.
 *   mask(string)    adds a string to the redaction set (a secret output, or a
 *                   string built from a secret).
 *   check(text)     true, or nil and why, for whether text is an envelope. */
#ifndef QWE_KERNEL_LUASECRETS_H
#define QWE_KERNEL_LUASECRETS_H

#include <lua.h>

int luaopen_qwe_secrets(lua_State *L);

#endif
