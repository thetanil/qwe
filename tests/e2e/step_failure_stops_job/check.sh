! grep -rq NEVER RUN "$QWE_STDOUT" || { echo "a step after the failure ran" >&2; exit 1; }
