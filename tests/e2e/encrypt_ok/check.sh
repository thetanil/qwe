# one line: the envelope, and the plaintext is nowhere in it
[ "$(wc -l < "$QWE_STDOUT")" = 1 ] || { echo "not one line" >&2; exit 1; }
grep -q '^qwe:1:xchacha20poly1305:' "$QWE_STDOUT" || { echo "no envelope prefix" >&2; exit 1; }
! grep -q hunter2 "$QWE_STDOUT" || { echo "plaintext in the output" >&2; exit 1; }
