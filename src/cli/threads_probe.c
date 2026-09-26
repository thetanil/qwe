/* Not qwe: a program that does start a thread, so that no_threads_test.sh can
 * show its check sees one. */
#include <pthread.h>

static void *nothing(void *arg)
{
	return arg;
}

int main(void)
{
	pthread_t t;

	if (pthread_create(&t, NULL, nothing, NULL) != 0)
		return 1;
	return pthread_join(t, NULL) != 0;
}
