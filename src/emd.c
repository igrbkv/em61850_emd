#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#define _GNU_SOURCE
#define _DEFAULT_SOURCE		// for daemon
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <uv.h>

#include "emd.h"
#include "calc.h"
#include "tcp_server.h"
#include "adc_client.h"
#include "sync_client.h"
#include "sv_read.h"
#include "log.h"
#include "settings.h"

#define EMD_CONFFILE EMD_CONFPATH "/emd.conf"
#define EMD_PIDFILE "/var/run/emd.pid" 


static void default_init();
static int handle_cmdline(int *argc, char ***argv);
static void open_log(void);
static void clean_exit(int sig);
static void reload_conf(int sig);
static int create_pidfile();
int emd_read_conf(const char *file);
int check_conf();


const char *progname;
const char *emd_confpath = EMD_CONFPATH;
static const char *pidfile = EMD_PIDFILE; 
char *conffile;

uv_loop_t *loop = NULL;

static int foreground;
static int restart;
 

int main(int argc, char **argv)
{
	int exit_status = EXIT_FAILURE;
	/* learn who we really are */
	progname = (const char *)strrchr(argv[0], '/');
	progname = progname ? (progname + 1) : argv[0];

	default_init();
	handle_cmdline(&argc, &argv);

	/* read in our configuration */
	if (emd_read_conf(conffile) == -1)
		goto end;

	open_log();

	if (!foreground) {
		if (daemon(0, 0) == -1) {
			emd_log(LOG_ERR, "daemon(): %s", strerror(errno));
			goto end;
		}
	}
	
	/* trap key signals */
	signal(SIGHUP, reload_conf);
	signal(SIGINT, clean_exit);
	signal(SIGQUIT, clean_exit);
	signal(SIGTERM, clean_exit);
	signal(SIGPIPE, SIG_IGN);

	/* create our pidfile */
	if (!foreground && create_pidfile() < 0)
		goto end;

	loop = uv_default_loop();

	if (calc_init() == -1 ||
		tcp_server_init() == -1 ||
		adc_client_init() == -1 ||
		sync_client_init() == -1 ||
		sv_read_init() == -1)
		goto  end;

	emd_log(LOG_INFO, "started");		

	uv_run(loop, UV_RUN_DEFAULT);

	exit_status = EXIT_SUCCESS;

end:	
	clean_exit_with_status(exit_status);

	return 0;
}

static void default_init()
{
	conffile = EMD_CONFFILE;
    foreground = 0;
	emd_debug = 0;

	set_default_settings();
}

static void open_log(void)
{
	int log_opts;

	/* open the syslog */
	log_opts = LOG_CONS|LOG_NDELAY;
	if (emd_debug) {
		log_opts |= LOG_PERROR;
	}
	openlog(PACKAGE, log_opts, LOG_DAEMON);
}

static void cleanup()
{	
	if (loop != NULL) {
		uv_loop_close(loop);
		loop = NULL;
	}

	tcp_server_close();
	sync_client_close();
	adc_client_close();
	sv_read_close();
	calc_close();
}

void clean_exit_with_status(int status)
{
	cleanup();

	emd_log(LOG_NOTICE, "exiting");
	unlink(pidfile);
	exit(status);
}

static void clean_exit(int sig)
{
	emd_log(LOG_NOTICE, "clean exit: %d", sig);
	clean_exit_with_status(EXIT_SUCCESS);
}

static void reload_conf(int sig __attribute__((unused)))
{
	restart = 1;
	emd_log(LOG_NOTICE, "reloading configuration");
	cleanup();

	default_init();

	if (emd_read_conf(conffile) == -1)
		goto err;
	
	loop = uv_default_loop();
	if (calc_close() == -1 ||
		tcp_server_init() == -1 ||
		adc_client_init() == -1 ||
		sync_client_init() == -1 ||
		sv_read_init() == -1)
		goto err;
	uv_run(loop, UV_RUN_DEFAULT);

	return;
err:
	clean_exit_with_status(EXIT_FAILURE);
}

/*
 * Parse command line arguments
 */
static int handle_cmdline(int *argc, char ***argv)
{
	struct option opts[] = {
		{"conffile", 1, 0, 'c'},
		{"debug", 0, 0, 'd'},
		{"debug-foreground", 0, 0, 'D'},
		{"foreground", 0, 0, 'f'},
		{"version", 0, 0, 'v'},
		{"help", 0, 0, 'h'},
		{NULL, 0, 0, 0},
	};
	const char *opts_help[] = {
		"Set the configuration file.",	/* conffile */
		"Increase debugging level.",/* debug */
		"Increase debugging level (implies -f).",/* debug-foreground */
		"Run in the foreground.",		/* foreground */
		"Print version information.",		/* version */
		"Print this message.",			/* help */
	};
	struct option *opt;
	const char **hlp;
	//char *ptr;
	int max, size;

	for (;;) {
		int i;
		i = getopt_long(*argc, *argv,
		    "c:dDfvh", opts, NULL);
		if (i == -1) {
			break;
		}
		switch (i) {
		case 'c':
			conffile = optarg;
			break;
		case 'd':
			emd_debug++;
			break;
		case 'D':
			foreground = 1;
			emd_debug++;
            log_to_stderr = 1;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'v':
			printf(PACKAGE "-" VERSION "\n");
			exit(EXIT_SUCCESS);
		case 'h':
		default:
			fprintf(stderr, "Usage: %s [OPTIONS]\n", progname);
			max = 0;
			for (opt = opts; opt->name; opt++) {
				size = strlen(opt->name);
				if (size > max)
					max = size;
			}
			for (opt = opts, hlp = opts_help;
			     opt->name;
			     opt++, hlp++) {
				fprintf(stderr, "  -%c, --%s",
					opt->val, opt->name);
				size = strlen(opt->name);
				for (; size < max; size++)
					fprintf(stderr, " ");
				fprintf(stderr, "  %s\n", *hlp);
			}
			exit(EXIT_FAILURE);
			break;
		}
	}

	*argc -= optind;
	*argv += optind;

	return 0;
}



int create_pidfile()
{
	int fd;

	/* JIC */
	unlink(pidfile);

	/* open the pidfile */
	fd = open(pidfile, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fd >= 0) {
		FILE *f;

		/* write our pid to it */
		f = fdopen(fd, "w");
		if (f != NULL) {
			fprintf(f, "%d\n", getpid());
			fclose(f);
			/* leave the fd open */
			return 0;
		}
		close(fd);
	}

	/* something went wrong */
	emd_log(LOG_ERR, "can't create pidfile %s: %s",
		    pidfile, strerror(errno));
	return -1;
}
