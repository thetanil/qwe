#define _GNU_SOURCE
#include "src/kernel/proc.h"
#include "src/kernel/gcov.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

int qwe_proc_spawn(struct qwe_proc *p, qwe_child_fn fn, void *arg)
{
	int fds[2], res[2];
	pid_t pid;

	if (pipe(fds) < 0) {
		p->fail_op = "pipe";
		return -1;
	}
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	if (pipe2(res, O_CLOEXEC) < 0) {
		int saved = errno;

		p->fail_op = "pipe";
		close(fds[0]);
		close(fds[1]);
		errno = saved;
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		int saved = errno;

		p->fail_op = "fork";
		close(fds[0]);
		close(fds[1]);
		close(res[0]);
		close(res[1]);
		errno = saved;
		return -1;
	}
	if (pid == 0) {
		sigset_t none;
		char **argv;
		int null;

		setpgid(0, 0);
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		null = open("/dev/null", O_RDONLY);
		if (null >= 0) {
			dup2(null, 0);
			close(null);
		}
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		close(fds[0]);
		close(fds[1]);
		close(res[0]);
		argv = fn(arg, res[1]);
		qwe_gcov_dump();
		if (!argv)
			_exit(126);
		execvp(argv[0], argv);
		_exit(127);
	}
	/* Both sides call setpgid so the group exists before either proceeds. */
	setpgid(pid, pid);
	close(fds[1]);
	close(res[1]);
	fcntl(fds[0], F_SETFL, O_NONBLOCK);
	fcntl(res[0], F_SETFL, O_NONBLOCK);
	p->res_fd = res[0];
	p->pid = pid;
	p->out_fd = fds[0];
	return 0;
}

int qwe_proc_kill_group(const struct qwe_proc *p, int sig)
{
	return kill(-p->pid, sig);
}

int qwe_proc_become_subreaper(void)
{
	return prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
}

int qwe_proc_group_empty(pid_t pgid)
{
	int saved = errno;
	int gone;

	while (waitpid(-pgid, NULL, WNOHANG) > 0)
		;
	/* Signal 0 checks that a process of the group exists, and sends nothing. */
	gone = kill(-pgid, 0) < 0 && errno == ESRCH;
	errno = saved;
	return gone;
}
