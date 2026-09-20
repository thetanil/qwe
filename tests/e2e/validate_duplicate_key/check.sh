# qwe run rejects it too, with the same errors and no run directory.
"$QWE_BIN" run w.yaml 2>err
[ $? -eq 2 ] || exit 1
grep -q 'w.yaml:6:1: duplicate key "jobs" (first at line 1)' err || exit 1
[ ! -e .qwe ] || exit 1
