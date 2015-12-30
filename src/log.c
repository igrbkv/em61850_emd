#include <stdio.h>
#define _BSD_SOURCE
#include <syslog.h>
#include <stdarg.h>

#include "log.h"

int emd_debug = 0;
int log_to_stderr = 0;

int
#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
emd_log(int level, const char *fmt, ...)
{
	va_list args;

	if (level == LOG_DEBUG && !emd_debug)
		return 0;

	/* if "-f" has been specified */
	if (log_to_stderr) {
		/* send debug output to stderr */
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);

		fprintf(stderr, "\n");
	} else {
		va_start(args, fmt);
		vsyslog(level, fmt, args);
		va_end(args);
	}

	return 0;
}
