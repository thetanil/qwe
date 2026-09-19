# The file is really there, with the content and mode asked for.
[ "$(cat out.txt)" = hello ]
[ "$(stat -c %a out.txt)" = 640 ]
