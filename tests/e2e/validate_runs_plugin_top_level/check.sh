# See CONTEXT.md, "Trust boundary": validating loads a project plugin's module, which
# runs its top level (and only that). If this starts failing, validation stopped
# executing plugin code: m1-review/11 is done, and this case should say so.
[ "$(cat top-level-ran)" = yes ] || { echo "the plugin's top level did not run" >&2; exit 1; }
[ ! -e check-ran ] && [ ! -e apply-ran ] || { echo "validate ran a plugin's check or apply" >&2; exit 1; }
