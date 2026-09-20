# Nothing was put in the refused directory, and it was left as it was.
[ -z "$(ls xdgrefused/qwe)" ] || exit 1
[ "$(stat -c %a xdgrefused/qwe)" = 777 ]
