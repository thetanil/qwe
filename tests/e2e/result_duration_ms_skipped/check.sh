job_ms=$(jq '.jobs.b.duration_ms' result.raw.json)
step_ms=$(jq '.jobs.b.steps[0].duration_ms' result.raw.json)

[ "$job_ms" = null ] || { echo "skipped job duration_ms is $job_ms, want null" >&2; exit 1; }
[ "$step_ms" = null ] || { echo "skipped step duration_ms is $step_ms, want null" >&2; exit 1; }
