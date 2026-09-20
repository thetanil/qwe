# Nothing was put in the refused directory, and it was left as it was.
[ -z "$(ls /tmp/qwe-e2e-refused)" ] || exit 1
[ "$(stat -c %a /tmp/qwe-e2e-refused)" = 777 ] || exit 1
rm -rf /tmp/qwe-e2e-refused
