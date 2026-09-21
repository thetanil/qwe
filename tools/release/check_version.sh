#!/bin/sh
# usage: check_version.sh <tag> <qwe binary> [qwe.h]
# The release's version lives in one place, QWE_VERSION in src/kernel/qwe.h. Fails unless the
# tag is "v" + that, and the binary's --version prints "qwe <that>".
tag=$1 bin=$2 hdr=${3:-src/kernel/qwe.h}
[ -n "$tag" ] && [ -x "$bin" ] || { echo "usage: check_version.sh <tag> <qwe binary> [qwe.h]" >&2; exit 3; }
want=$(sed -n 's/^#define QWE_VERSION "\(.*\)"$/\1/p' "$hdr")
[ -n "$want" ] || { echo "check_version: no QWE_VERSION in $hdr" >&2; exit 3; }
if [ "$tag" != "v$want" ]; then
	echo "check_version: tag is $tag but QWE_VERSION is $want (want v$want)" >&2
	exit 1
fi
got=$("$bin" --version)
if [ "$got" != "qwe $want" ]; then
	echo "check_version: binary prints '$got' but QWE_VERSION is $want (want 'qwe $want')" >&2
	exit 1
fi
