/* qwe.fs: the little filesystem access plugin loading needs (no luafilesystem). */
#ifndef QWE_KERNEL_LUAFS_H
#define QWE_KERNEL_LUAFS_H

#include <lua.h>

/* Opens the module: qwe.fs.list(dir) -> sorted array of entry names (without
 * . and ..), or nil, message; qwe.fs.isdir(path) -> boolean. */
int luaopen_qwe_fs(lua_State *L);

#endif
