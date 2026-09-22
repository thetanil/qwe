#!/bin/sh
# usage: run_case.sh <qwe-binary> <case-dir>
#
# Case dir layout:
#   args              one argument per line (optional)
#   expected/stdout   golden stdout   (optional; checked if present)
#   expected/stderr   golden stderr   (optional; checked if present)
#   expected/exit     expected exit code (default 0)
#   expected/<path>   any other file: compared with <path> in the work dir
#   env               optional KEY=VALUE lines, exported to qwe
#                     (QWE_E2E_MARK, a number unique to the run, is always set)
#   ulimit            optional arguments for ulimit, e.g. "-u 1": qwe runs under that
#                     limit, so a real fork can fail. Skipped (passes) as root,
#                     which ignores the process limit.
#   signal            optional: qwe runs in the background and gets a signal
#                     (kill -SIGNAME). Either "<seconds> <SIGNAME>": after the delay,
#                     or "file <SIGNAME> <path>...": once every path has appeared in
#                     the work dir (the step creates it, so no delay guesses when it
#                     is running). Waiting gives up after 30 s and fails the case.
#   asan-options      optional: extra ASAN_OPTIONS, appended under --config=asan
#   needs-ssh         optional marker: the case needs the devcontainer's ssh target. It
#                     is skipped (prints SKIP, passes) unless REMOTE_CONTAINERS is set
#                     and `ssh -o BatchMode=yes 172.18.0.1 true` works
#                     With QWE_E2E_REQUIRE_SSH=1 (CI) an unreachable target fails the case instead.
#   setup.sh          optional: run in the work dir before qwe (chmod a key file, say)
#   stdin             optional: qwe's standard input (a file; setup.sh may create or replace
#                     it, and a directory there makes reading stdin fail with EISDIR)
#   bin/              optional directory of commands put first on PATH (a shim)
#   check.sh          optional extra assertions, run in the work dir after qwe;
#                     sees $QWE_STDOUT (qwe's stdout) and must exit 0
#                     ($QWE_BIN is the qwe binary, to run it again)
#   everything else   input files, copied to a scratch work dir where qwe runs
#
#
# If qwe left exactly one .qwe/runs/<id>/, that directory is renamed to RUN/ and
# result.json has its run id and times replaced by RUN and TIME, its duration_ms
# values by DURATION, and the first field (the time) of each lifecycle.trace line
# by TIME, so goldens can say expected/RUN/j.log, expected/RUN/result.json and
# expected/RUN/lifecycle.trace.
# check.sh sees the trace with its real times as lifecycle.raw (seconds since the
# run started, first field), and result.json with its real numbers as result.raw.json.
# If qwe wrote summary.md (--summary summary.md), its duration column and its
# heading's "(N ms)" are also masked to DURATION, so goldens can say expected/summary.md.
#
#
# Exit: 0 pass, 1 golden mismatch, 3 harness error.
qwe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
case_dir=$2
[ -x "$qwe" ] || { echo "run_case: no binary $qwe" >&2; exit 3; }
[ -d "$case_dir" ] || { echo "run_case: no case dir $case_dir (pwd $(pwd))" >&2; exit 3; }

case_dir=$(cd "$case_dir" && pwd)
if [ -f "$case_dir/needs-ssh" ]; then
	ssh_err=
	if [ -z "$REMOTE_CONTAINERS" ] || ! ssh_err=$(ssh -o BatchMode=yes -o ConnectTimeout=5 172.18.0.1 true 2>&1); then
		if [ -n "$QWE_E2E_REQUIRE_SSH" ]; then
			echo "FAIL: $(basename "$case_dir"): needs ssh to 172.18.0.1 and QWE_E2E_REQUIRE_SSH is set, but the host is not reachable (REMOTE_CONTAINERS unset, or ssh failed)" >&2
			echo "REMOTE_CONTAINERS=${REMOTE_CONTAINERS:-(unset)} SSH_AUTH_SOCK=${SSH_AUTH_SOCK:-(unset)} HOME=${HOME:-(unset)}; ssh said: $ssh_err" >&2
			exit 1
		fi
		echo "SKIP: $(basename "$case_dir"): needs ssh to 172.18.0.1 (REMOTE_CONTAINERS unset, or the host is not reachable)" >&2
		exit 0
	fi
fi
work=$(mktemp -d) || exit 3
trap 'rm -rf "$work"' EXIT
cp -R "$case_dir"/. "$work"/ || exit 3
rm -rf "$work/expected" "$work/args"
rm -f "$work/check.sh" "$work/env" "$work/signal" "$work/ulimit" "$work/needs-ssh" "$work/setup.sh" "$work/asan-options"
[ -d "$work/bin" ] && PATH="$work/bin:$PATH"

[ -f "$case_dir/setup.sh" ] && { (cd "$work" && sh "$case_dir/setup.sh") || exit 3; }
set --
if [ -f "$case_dir/args" ]; then
	while IFS= read -r line; do set -- "$@" "$line"; done < "$case_dir/args"
fi

want_exit=0
[ -f "$case_dir/expected/exit" ] && want_exit=$(cat "$case_dir/expected/exit")

# A number unique to this run, for a step to sleep on, so a check can tell its
# own leftovers from those of tests running alongside.
export QWE_E2E_MARK=7$$
limit=
[ -f "$case_dir/ulimit" ] && limit=$(cat "$case_dir/ulimit")
# Root ignores RLIMIT_NPROC, so a limit case cannot fail its fork as root. qwe
# is not meant to run as root anyway: the case is skipped (and passes).
if [ -n "$limit" ] && [ "$(id -u)" = 0 ]; then
	echo "run_case: skipped: a ulimit case cannot run as root" >&2
	exit 0
fi
# Replaces the calling shell with qwe, under the case's ulimit if it has one.
# (bash: the /bin/sh here is dash, whose ulimit has no -u.)
run_qwe() {
	# stdin: <work>/stdin, if it exists once setup.sh has run (a directory makes read(2) fail)
	if [ -e "$work/stdin" ]; then
		exec <"$work/stdin"
	fi
	if [ -n "$limit" ]; then
		exec bash -c 'ulimit $1 && shift && exec "$@"' bash "$limit" "$qwe" "$@"
	fi
	exec "$qwe" "$@"
}
# a case never inherits the runner's XDG_RUNTIME_DIR: its owner and mode differ per machine
unset XDG_RUNTIME_DIR
# Under --config=asan, a report from qwe or any step child would land on the
# stderr/stdout the goldens compare. Send reports to per-pid files instead, in
# the test's undeclared outputs (kept by bazel), never the scratch dir.
if [ -n "$ASAN_OPTIONS" ]; then
	report_dir=${TEST_UNDECLARED_OUTPUTS_DIR:-${TMPDIR:-/tmp}}
	ASAN_OPTIONS="$ASAN_OPTIONS:log_path=$report_dir/asan-$(basename "$case_dir")"
	# extra options for a case that needs them (a deliberate SIGSEGV, say)
	[ -f "$case_dir/asan-options" ] && ASAN_OPTIONS="$ASAN_OPTIONS:$(cat "$case_dir/asan-options")"
	# A process limit starves LeakSanitizer of the thread it needs; it dies with a
	# fatal error of its own, so the ulimit cases run without leak detection.
	[ -n "$limit" ] && ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
	export ASAN_OPTIONS
fi
if [ -f "$case_dir/env" ]; then
	while IFS= read -r line; do [ -n "$line" ] && export "$line"; done < "$case_dir/env"
fi
if [ -f "$case_dir/signal" ]; then
	# Run qwe in the background, send it a signal, wait for it. The signal goes
	# after a delay, or once the named files have appeared in the work dir.
	read -r sig_first sig_second sig_files < "$case_dir/signal"
	(cd "$work" && run_qwe "$@" >"$work.stdout" 2>"$work.stderr") &
	qwe_pid=$!
	if [ "$sig_first" = file ]; then
		sig_name=$sig_second
		waited=0
		for f in $sig_files; do
			while [ ! -e "$work/$f" ]; do
				kill -0 "$qwe_pid" 2>/dev/null || break 2 # qwe is gone: nothing to signal
				waited=$((waited + 1))
				if [ "$waited" -gt 1500 ]; then # 30 s, only ever reached when the case is broken
					kill -KILL "$qwe_pid" 2>/dev/null
					echo "run_case: $f never appeared in the work dir" >&2
					exit 3
				fi
				sleep 0.02
			done
		done
	else
		sig_name=$sig_second
		sleep "$sig_first"
	fi
	kill "-$sig_name" "$qwe_pid" 2>/dev/null
	wait "$qwe_pid"
	got_exit=$?
else
	(cd "$work" && run_qwe "$@" >"$work.stdout" 2>"$work.stderr")
	got_exit=$?
fi

# Give the one run directory a fixed name and blank its run id and times.
if [ -d "$work/.qwe/runs" ] && [ "$(ls "$work/.qwe/runs" | wc -l)" = 1 ]; then
	mv "$work/.qwe/runs/"* "$work/RUN"
	if [ -f "$work/RUN/result.json" ]; then
		# check.sh can still measure: the unblanked result is kept as result.raw.json
		cp "$work/RUN/result.json" "$work/result.raw.json"
		sed -e 's/"run_id": "[^"]*"/"run_id": "RUN"/' \
		    -e 's/"started": "[^"]*"/"started": "TIME"/' \
		    -e 's/"ended": "[^"]*"/"ended": "TIME"/' \
		    -e 's/"duration_ms": [0-9][0-9]*/"duration_ms": DURATION/' \
		    "$work/RUN/result.json" >"$work/RUN/result.json.new" &&
			mv "$work/RUN/result.json.new" "$work/RUN/result.json"
	fi
	if [ -f "$work/RUN/lifecycle.trace" ]; then
		# check.sh can still measure: the unblanked trace is kept as lifecycle.raw
		cp "$work/RUN/lifecycle.trace" "$work/lifecycle.raw"
		# the first field is the time since the run started
		sed -e 's/^[0-9][0-9]*\.[0-9][0-9]* /TIME /' "$work/RUN/lifecycle.trace" >"$work/RUN/lifecycle.trace.new" &&
			mv "$work/RUN/lifecycle.trace.new" "$work/RUN/lifecycle.trace"
	fi
fi
# --summary writes wherever the case's args point (conventionally summary.md); its
# duration column and heading duration are masked to DURATION, same as duration_ms.
if [ -f "$work/summary.md" ]; then
	sed -e 's/([0-9][0-9]* ms)/(DURATION ms)/' \
	    -e 's/| [0-9][0-9]* |$/| DURATION |/' \
	    "$work/summary.md" >"$work/summary.md.new" &&
		mv "$work/summary.md.new" "$work/summary.md"
fi

fail=0
if [ "$got_exit" != "$want_exit" ]; then
	echo "exit code: want $want_exit, got $got_exit" >&2
	fail=1
fi
check() { # <golden> <actual> <label>
	if ! diff -u "$1" "$2" >&2; then
		echo "mismatch: $3" >&2
		fail=1
	fi
}
[ -f "$case_dir/expected/stdout" ] && check "$case_dir/expected/stdout" "$work.stdout" stdout
[ -f "$case_dir/expected/stderr" ] && check "$case_dir/expected/stderr" "$work.stderr" stderr
: >"$work.files"
[ -d "$case_dir/expected" ] && (cd "$case_dir/expected" && find -L . -type f ! -name stdout ! -name stderr ! -name exit) >"$work.files"
while IFS= read -r f; do
	check "$case_dir/expected/$f" "$work/$f" "$f" || true
done <"$work.files"
if [ -f "$case_dir/check.sh" ]; then
	(cd "$work" && QWE_STDOUT="$work.stdout" QWE_BIN="$qwe" sh "$case_dir/check.sh") || { echo "check.sh failed" >&2; fail=1; }
fi
rm -f "$work.stdout" "$work.stderr" "$work.files"
[ "$fail" = 0 ]
