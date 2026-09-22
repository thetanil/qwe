grep -qF '***' summary.md || { echo "no redaction marker in summary.md" >&2; exit 1; }
! grep -qF 'my-canary-secret-42' summary.md || { echo "the secret leaked into summary.md" >&2; exit 1; }
