[ -z "$(ls /tmp/qwe-e2e-stale)" ] || { echo "left in the socket directory: $(ls /tmp/qwe-e2e-stale)" >&2; exit 1; }
rm -rf /tmp/qwe-e2e-stale
