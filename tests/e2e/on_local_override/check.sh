# the on: local step ran here (this directory); the later step, on self, read its output
[ "$(cat "$QWE_STDOUT")" = "[j] $(pwd)" ] || { echo "stdout: $(cat "$QWE_STDOUT"), want [j] $(pwd)" >&2; exit 1; }
