[ -z "$(ls xdg/qwe)" ] || { echo "left in the socket directory: $(ls xdg/qwe)" >&2; exit 1; }
