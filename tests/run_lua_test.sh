#!/bin/sh
# usage: run_lua_test.sh <luarun> <script.lua> [args...]
# Runs the script inside qwe's Lua state; exit status is the script's.
exec "$@"
