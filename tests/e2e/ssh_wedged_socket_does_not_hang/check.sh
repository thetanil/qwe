# The master was stopped, so every -O check and -O exit sent to it waited for an answer that
# never came. The run still ended (the bounds, 2 s each), and this is the wedged master's end.
pkill -9 -f '[s]sh -M -N -f -S /tmp/qwe-e2e-wedge/'
exit 0
rm -rf /tmp/qwe-e2e-wedge
