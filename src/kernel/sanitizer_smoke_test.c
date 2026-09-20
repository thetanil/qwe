/* Proves a sanitizer config is live: a child does something the sanitizer
 * must stop, and the test passes only if the child dies. Built only under
 * --config=ubsan or --config=asan (see the BUILD target), where a build that
 * silently dropped the flags would otherwise pass everything. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(QWE_SMOKE_UBSAN)
static void fault(int n)
{
	volatile int big = 2147483647;

	big += n; /* signed overflow */
	printf("%d\n", big);
}
#elif defined(QWE_SMOKE_ASAN)
static void fault(int n)
{
	char *p = malloc(4);

	p[4 + n] = 1; /* heap overflow */
	printf("%d\n", p[4 + n]);
	free(p);
}
#else
#error "build with QWE_SMOKE_UBSAN or QWE_SMOKE_ASAN"
#endif

int main(int argc, char **argv)
{
	int status;
	pid_t pid;

	(void)argv;
	pid = fork();
	if (pid < 0)
		return 2;
	if (pid == 0) {
		fault(argc); /* argc is 1: not a constant the compiler can fold */
		_exit(0);
	}
	if (waitpid(pid, &status, 0) < 0)
		return 2;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		fprintf(stderr, "sanitizer_smoke: the fault went unnoticed; the config is not live\n");
		return 1;
	}
	return 0;
}
