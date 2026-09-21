#!/bin/sh
# QWE_E2E_REQUIRE_SSH=1 makes an unreachable needs-ssh case fail instead of printing SKIP.
# A fake ssh that always fails stands in for an unreachable 172.18.0.1.
run_case=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
t=$(mktemp -d) || exit 3
trap 'rm -rf "$t"' EXIT
mkdir "$t/case" "$t/bin"
: >"$t/case/needs-ssh"
printf '#!/bin/sh\nexit 255\n' >"$t/bin/ssh"
chmod +x "$t/bin/ssh"
qwe=/bin/true

# without the switch: SKIP, pass
if ! out=$(PATH="$t/bin:$PATH" REMOTE_CONTAINERS=1 "$run_case" "$qwe" "$t/case" 2>&1); then
	echo "unreachable case must still skip: $out" >&2; exit 1
fi
case $out in *"SKIP: case"*) ;; *) echo "no SKIP line: $out" >&2; exit 1 ;; esac

# with it: fail, naming the case and the host
if out=$(PATH="$t/bin:$PATH" REMOTE_CONTAINERS=1 QWE_E2E_REQUIRE_SSH=1 "$run_case" "$qwe" "$t/case" 2>&1); then
	echo "must fail under QWE_E2E_REQUIRE_SSH=1" >&2; exit 1
fi
case $out in *case*172.18.0.1*) ;; *) echo "message lacks case and host: $out" >&2; exit 1 ;; esac
