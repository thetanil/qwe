# With QWE_LUA_COVERAGE set, qwe writes lcov to $COVERAGE_DIR: one file for
# itself and one for the child that runs the step, which execs away and would
# otherwise take its Lua coverage with it.
plugin=plugins/builtin/file.ensure/plugin.lua
n=$(ls cov/luacov-*.dat | wc -l)
[ "$n" -ge 2 ] || { echo "want a file per process, got $n" >&2; exit 1; }
# Some process ran the plugin's lines. Only the child does (checked below).
hit_in() { awk -v f="SF:$plugin" '$0==f{on=1;next} /^end_of_record/{on=0} on&&/^DA:/{split(substr($0,4),a,","); if(a[2]>0){print "y"; exit}}' "$1"; }
found=
for f in cov/luacov-*.dat; do [ "$(hit_in "$f")" = y ] && found="$found $f"; done
[ -n "$found" ] || { echo "no file records a hit in $plugin" >&2; exit 1; }
