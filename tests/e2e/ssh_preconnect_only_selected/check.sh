[ ! -e ssh.calls ] || { echo "ssh was called:" >&2; cat ssh.calls >&2; exit 1; }
