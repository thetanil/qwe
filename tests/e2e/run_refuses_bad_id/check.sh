# A bad id runs nothing: no run directory, no log, and the step never ran.
[ ! -e .qwe ] || { echo ".qwe was created" >&2; exit 1; }
[ ! -e ran ] || { echo "the step ran" >&2; exit 1; }
[ ! -e escaped.log ] || { echo "a log was written" >&2; exit 1; }
