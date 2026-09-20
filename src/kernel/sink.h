/* The log sink: each job's bytes go raw to <dir>/<job>.log, and line by line,
 * prefixed with the job id, to the terminal. */
#ifndef QWE_KERNEL_SINK_H
#define QWE_KERNEL_SINK_H

#include <stddef.h>

struct qwe_sink {
	const char *job; /* borrowed, not copied: it is the job's id, which must outlive the sink */
	int log_fd;
	int term_fd;
	char *line; /* the terminal's unfinished line */
	size_t line_len, line_cap;
};

/* Opens <dir>/<job>.log for writing. term_fd is where prefixed lines go. */
int qwe_sink_open(struct qwe_sink *s, const char *job, const char *dir, int term_fd);
void qwe_sink_write(struct qwe_sink *s, const void *data, size_t n);
/* Flushes an unfinished last line (adding its newline) and closes the log. */
void qwe_sink_close(struct qwe_sink *s);

#endif
