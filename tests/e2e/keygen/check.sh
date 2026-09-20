f=home/.config/qwe/secret
[ "$(stat -c %a "$f")" = 600 ] || { echo "mode: $(stat -c %a "$f")" >&2; exit 1; }
[ "$(wc -c < "$f")" = 32 ] || { echo "size: $(wc -c < "$f")" >&2; exit 1; }
before=$(cksum < "$f")
# a second call refuses, exits 2, and leaves the key alone
HOME=./home "$QWE_BIN" keygen 2>/dev/null
[ $? = 2 ] || { echo "second keygen did not exit 2" >&2; exit 1; }
[ "$(cksum < "$f")" = "$before" ] || { echo "the key was replaced" >&2; exit 1; }
