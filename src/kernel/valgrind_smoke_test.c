/* Proves the valgrind gate is live: a child branches on an uninitialised
 * value, which valgrind must report, and the test passes only if the child
 * then exits non-zero (--error-exitcode=1). Built as a test only under
 * --config=valgrind (see the BUILD target); a plain run would pass the fault
 * silently. ASan does not model uninitialised reads, so this is the class of
 * finding the gate exists for. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int fault(int n)
{
	int *p = malloc(sizeof *p);
	int r;

	if (!p)
		return 0;
	r = *p + n > 0; /* uninitialised read decides a branch */
	free(p);
	if (r)
		puts("positive");
	return r;
}

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
		fprintf(stderr, "valgrind_smoke: the uninitialised read went unnoticed; the gate is not live\n");
		return 1;
	}
	return 0;
}
