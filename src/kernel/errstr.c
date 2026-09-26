#define _POSIX_C_SOURCE 200809L /* the XSI strerror_r (int result), not the GNU one */
#include "src/kernel/errstr.h"

#include "src/kernel/fmt.h"

#include <errno.h>
#include <string.h>

const char *qwe_strerror(int err)
{
	static __thread char buf[64];
	int saved = errno; /* the caller reads errno after us, for the next message */

	if (strerror_r(err, buf, sizeof buf) != 0)
		qwe_xfmt(buf, sizeof buf, "Unknown error %d", err);
	errno = saved;
	return buf;
}
