#include "src/kernel/sink.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_all(int fd, const char *p, size_t n)
{
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		p += w;
		n -= (size_t)w;
	}
}

int qwe_sink_open(struct qwe_sink *s, const char *job, const char *dir, int term_fd)
{
	size_t n = strlen(dir) + strlen(job) + 8;
	char *path = malloc(n);

	memset(s, 0, sizeof *s);
	if (!path)
		return -1;
	snprintf(path, n, "%s/%s.log", dir, job);
	s->log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	free(path);
	if (s->log_fd < 0)
		return -1;
	s->job = job;
	s->term_fd = term_fd;
	return 0;
}

static void emit_line(struct qwe_sink *s)
{
	char *out = malloc(strlen(s->job) + s->line_len + 4);

	if (out) {
		size_t n = (size_t)sprintf(out, "[%s] ", s->job);
		memcpy(out + n, s->line, s->line_len);
		n += s->line_len;
		out[n++] = '\n';
		write_all(s->term_fd, out, n);
		free(out);
	}
	s->line_len = 0;
}

void qwe_sink_write(struct qwe_sink *s, const void *data, size_t n)
{
	const char *p = data;
	size_t i;

	write_all(s->log_fd, p, n);
	for (i = 0; i < n; i++) {
		if (p[i] == '\n') {
			emit_line(s);
			continue;
		}
		if (s->line_len == s->line_cap) {
			size_t cap = s->line_cap ? s->line_cap * 2 : 256;
			char *grown = realloc(s->line, cap);
			if (!grown)
				return;
			s->line = grown;
			s->line_cap = cap;
		}
		s->line[s->line_len++] = p[i];
	}
}

void qwe_sink_close(struct qwe_sink *s)
{
	if (s->line_len > 0)
		emit_line(s);
	free(s->line);
	close(s->log_fd);
	memset(s, 0, sizeof *s);
}
