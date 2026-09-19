# An invalid workflow runs nothing: no run directory, and the step never ran.
[ ! -e .qwe ] || { echo ".qwe was created" >&2; exit 1; }
[ ! -e ran ] || { echo "the step ran" >&2; exit 1; }
