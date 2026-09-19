seq 1 50000 | sed 's/^/A /' | cmp - RUN/a.log || { echo "a.log is not exactly a's lines" >&2; exit 1; }
seq 1 50000 | sed 's/^/B /' | cmp - RUN/b.log || { echo "b.log is not exactly b's lines" >&2; exit 1; }
