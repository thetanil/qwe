# The five outputs are right: exists, content, sha256 (against the real file),
# mode, and owner (against the user running the test).
sha=$(sha256sum out.txt | cut -d' ' -f1)
owner=$(id -un)
grep -qx 'exists=true' out.env || { echo "exists not true"; exit 1; }
grep -qx 'content=hi' out.env || { echo "content wrong"; exit 1; }
grep -qx "sha256=$sha" out.env || { echo "sha256 wrong"; exit 1; }
grep -qx 'mode=0644' out.env || { echo "mode wrong"; exit 1; }
grep -qx "owner=$owner" out.env || { echo "owner wrong"; exit 1; }
