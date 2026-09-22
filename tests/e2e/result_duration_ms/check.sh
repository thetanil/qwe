# result.raw.json has the real numbers; result.json (checked against the golden) has them masked.
step_ms=$(jq '.jobs.j.steps[0].duration_ms' result.raw.json)
job_ms=$(jq '.jobs.j.duration_ms' result.raw.json)

[ "$step_ms" -ge 1000 ] && [ "$step_ms" -le 3000 ] || { echo "step duration_ms $step_ms not in [1000, 3000]" >&2; exit 1; }
[ "$job_ms" -ge "$step_ms" ] || { echo "job duration_ms $job_ms < step duration_ms $step_ms" >&2; exit 1; }

for f in jobs.j.duration_ms jobs.j.steps[0].duration_ms; do
	jq -e ".$f | type == \"number\"" result.raw.json >/dev/null || { echo "$f is not a number" >&2; exit 1; }
done
