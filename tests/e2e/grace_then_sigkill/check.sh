# The step ignored SIGTERM, so only SIGKILL after the grace period ended it, and
# it took the whole group with it: the stubborn shell is gone.
pid=$(cat shell.pid) || { echo "the step never recorded its pid" >&2; exit 1; }
[ ! -d "/proc/$pid" ] || { echo "the stubborn shell ($pid) is still alive" >&2; exit 1; }
