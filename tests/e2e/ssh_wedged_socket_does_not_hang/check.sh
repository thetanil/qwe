# The master was stopped, so every -O check and -O exit sent to it waited for an answer that
# never came. The run still ended (the bounds, 2 s each), and this is the wedged master's end.
pkill -9 -f '[s]sh -M -N -f -S xdg/qwe/'
exit 0
