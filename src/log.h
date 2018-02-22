#ifndef LOG_H_
#define LOG_H_

/* for LOG_ERR, LOG_DEBUG, LOG_INFO, etc... */
#include <syslog.h>
#include <stdint.h>

/*
 * Set to 1 to send LOG_DEBUG logging to stderr, zero to ignore LOG_DEBUG
 * logging.  Default is zero.
 */

extern int emd_debug;
extern int log_to_stderr;

extern int emd_log(int level, const char *fmt, ...) __attribute__((format(printf,2,3)));

extern void dump_buf(uint8_t *buf, int sz);
#endif /* LOG_H__ */
