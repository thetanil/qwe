# The child ignored SIGTERM, so only SIGKILL at the end of its grace period
# could stop it. It is gone.
pid=$(cat child.pid) || { echo "the step never recorded its child's pid" >&2; exit 1; }
[ ! -d "/proc/$pid" ] || { echo "the child ($pid) is still alive" >&2; exit 1; }
