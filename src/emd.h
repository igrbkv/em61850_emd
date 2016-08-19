#ifndef EMD_H_
#define EMD_H_

#define PACKAGE "emd"

extern int emd_debug;
extern const char *progname;

#define EMD_CONFPATH "/etc/opt/EM61850"
extern const char *emd_confpath;
extern char *conffile;

extern void clean_exit_with_status(int status);

#endif /* EMD_H_ */
