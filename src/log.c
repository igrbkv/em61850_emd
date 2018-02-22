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

void dump_buf(uint8_t *buf, int sz)
{
	if (emd_debug > 0) {
		emd_log(LOG_DEBUG, "bufsize=%d", sz);
		for (int i = 0; i < sz; i += 8)
			emd_log(LOG_DEBUG, "0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x", buf[i], buf[i+1], buf[i+2], buf[i+3], buf[i+4], buf[i+5], buf[i+6], buf[i+7]);
	}
}
