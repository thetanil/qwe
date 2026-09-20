# Nothing was put in the refused directory, and it was left as it was.
[ -z "$(ls xdg/qwe)" ] || exit 1
[ "$(stat -c %a xdg/qwe)" = 777 ]
