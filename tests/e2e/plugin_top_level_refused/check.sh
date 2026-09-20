# The plugin is refused, and nothing of it ran, under validate and under run alike.
[ ! -e top-level-ran ] || { echo "the plugin's top level ran during validate" >&2; exit 1; }
"$QWE_BIN" run w.yaml 2>err; [ $? -eq 2 ] || { echo "run did not refuse the plugin" >&2; exit 1; }
grep -q 'plugin.lua:2:11: top-level code is not allowed' err || { cat err >&2; exit 1; }
[ ! -e top-level-ran ] && [ ! -e .qwe/runs ] || { echo "run ran the plugin or made a run directory" >&2; exit 1; }
