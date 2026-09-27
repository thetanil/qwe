# qwe --version prints "qwe <QWE_VERSION>", with the version read from src/kernel/qwe.h, the
# one place it is written: a release bumps the header and nothing else. The parse is the one
# tools/release/check_version.sh uses. run_case.sh runs this file by its absolute path in the
# runfiles tree, so the header is three directories up from here.
hdr=$(dirname "$0")/../../../src/kernel/qwe.h
want=$(sed -n 's/^#define QWE_VERSION "\(.*\)"$/\1/p' "$hdr")
[ -n "$want" ] || { echo "cli_version: no QWE_VERSION in $hdr" >&2; exit 1; }
[ "$(wc -l <"$QWE_STDOUT")" -eq 1 ] || { echo "cli_version: want exactly one line of output" >&2; cat "$QWE_STDOUT" >&2; exit 1; }
got=$(cat "$QWE_STDOUT")
[ "$got" = "qwe $want" ] || { echo "cli_version: qwe --version printed '$got', want 'qwe $want' (QWE_VERSION in $hdr)" >&2; exit 1; }
