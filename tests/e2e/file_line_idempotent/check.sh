# The file has the edited content and the mode file.ensure set, untouched by file.line.
[ "$(cat out.txt)" = "$(printf 'a=99\nc=3\nd=4')" ]
[ "$(stat -c %a out.txt)" = 640 ]
