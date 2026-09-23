# A missing file is not an error: exists=false, every other output empty.
cat out.env
[ "$(cat out.env)" = "exists=false
content=
sha256=
mode=
owner=" ]
