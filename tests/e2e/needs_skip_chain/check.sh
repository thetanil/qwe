# Skipped jobs never started, so they have no log, and nothing they would have printed exists.
[ ! -e RUN/b.log ] && [ ! -e RUN/c.log ] || { echo "a skipped job has a log" >&2; exit 1; }
! grep -rq NEVER RUN || { echo "a skipped job's step ran" >&2; exit 1; }
