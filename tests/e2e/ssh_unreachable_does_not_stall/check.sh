# the time from a step starting to its timeout firing, from the real trace
delta() { awk -v j="$1" '$2==j && $5=="next-step" {s=$1} $2==j && $5=="step-timeout" {e=$1} END {printf "%.3f", e - s}' lifecycle.raw; }
within() { awk -v d="$1" -v want="$2" 'BEGIN {x = d - want; if (x < 0) x = -x; exit !(x <= 0.1)}'; }
d=$(delta a)
within "$d" 1 || { echo "a 1 s timeout fired after $d s: the unreachable target stalled it" >&2; exit 1; }
