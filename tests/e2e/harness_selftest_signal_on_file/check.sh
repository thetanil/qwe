# The harness waited for the marker, which the step creates a second after it
# starts. A signal on a delay guess would have come before it: the step would
# have been cancelled without ever creating the marker.
[ -e marker ] || { echo "the signal was sent before the marker appeared" >&2; exit 1; }
grep -q '"outcome": "cancelled"' RUN/result.json || { echo "the job was not cancelled" >&2; exit 1; }
