/* qwe.fs: the little filesystem access plugin loading needs (no luafilesystem). */
#ifndef QWE_KERNEL_LUAFS_H
#define QWE_KERNEL_LUAFS_H

#include <lua.h>

/* Opens the module: qwe.fs.list(dir) -> sorted array of entry names (without
 * . and ..), or nil, message; qwe.fs.isdir(path) -> boolean;
 * qwe.fs.private_dir(path, what) -> true, or nil, message: makes the directory
 * 0700 if it is missing, then requires a real directory that is owned by this
 * user with no group or other access (what names it in the message). */
int luaopen_qwe_fs(lua_State *L);

#endif
