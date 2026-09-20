# The socket directory was made under XDG_RUNTIME_DIR, private, and the step ran.
[ "$(stat -c %a xdg/qwe)" = 700 ] || exit 1
[ -n "$(cat "$QWE_STDOUT")" ]
