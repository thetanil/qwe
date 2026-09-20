# qwe validate reads a project plugin (luacheck, compile) but runs none of it: not its
# top level, not check, not apply. (`qwe run` loads the module when a step uses it.)
[ ! -e top-level-ran ] && [ ! -e check-ran ] && [ ! -e apply-ran ] || {
	echo "validate ran plugin code: $(ls top-level-ran check-ran apply-ran 2>/dev/null | tr '\n' ' ')" >&2
	exit 1
}
