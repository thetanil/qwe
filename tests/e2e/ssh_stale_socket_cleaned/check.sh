[ -z "$(ls xdgstale/qwe)" ] || { echo "left in the socket directory: $(ls xdgstale/qwe)" >&2; exit 1; }
