# The step succeeded, but the child it left behind got SIGTERM, and qwe waited
# for it: nothing is left alive.
pid=$(cat child.pid) || { echo "the step never recorded its child's pid" >&2; exit 1; }
[ ! -d "/proc/$pid" ] || { echo "the background child ($pid) is still alive" >&2; exit 1; }
