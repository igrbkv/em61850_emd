#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <sys/types.h>

#include "emd.h"
#include "log.h"
#include "settings.h"
#include "calc.h"

/* Настройки энергомонитора:
 * external_stream1 = 0 - (по умолчанию) 
 *			используется ADC
 *					  1 - используется внешний 
 *			поток 1
 *		src_mac_external_stream1 - mac источника внешнего потока 1
 *		dst_mac_external_stream1 - mac назначения внешнего потока 1
 *		sv_id_external_stream1 - sv_id внешнего 
 *			потока 1
 * external_stream2 = 0 - (по умолчанию) внешний 
 *						поток 2 не используется
 *					  1 - внешний поток 2 
 *			используется 
 *		src_mac_external_stream2 - mac источника внешнего потока 2
 *		dst_mac_external_stream2 - mac назначения внешнего потока 2
 *		sv_id_external_stream2 - sv_id внешнего потока 2
 * X_trans_coef_streamN = 1 - по умолчанию
 */

int dump;
unsigned short emd_port;
stream_property streams_prop[2]; 

void set_default_settings()
{
	emd_port = EMD_PORT;
	dump = 0;
	memset(streams_prop, 0, sizeof(stream_property)*2);
}

int emd_read_conf(const char *file)
{
	FILE *fp;
	char buf[512];
	int line = 0;


    /* r - read-only */
	fp = fopen(file, "r");
	if (!fp) {
		emd_log(LOG_ERR, "fopen(%s): %s", file, strerror(errno));
		return -1;
	}

	/* read each line */
	while (!feof(fp) && !ferror(fp)) {
		char *p = buf;	//, *_p;
		char key[64];
		char val[512];
		int n;

		line++;
		memset(key, 0, sizeof(key));
		memset(val, 0, sizeof(val));

		if (fgets(buf, sizeof(buf)-1, fp) == NULL) {
			continue;
		}

		/* skip leading whitespace */
		while (*p && isspace((int)*p)) {
			p++;
		}
		/* blank lines and comments get ignored */
		if (!*p || *p == '#') {
			continue;
		}

		/* quick parse */
		n = sscanf(p, "%63[^=\n]=%255[^\n]", key, val);
		if (n != 2) {
			emd_log(LOG_WARNING, "can't parse %s at line %d",
			    file, line);
			continue;
		}
		if (emd_debug >= 3) {
			emd_log(LOG_DEBUG, "    key=\"%s\" val=\"%s\"",
			    key, val);
		}
		/* handle the parsed line */
		if (!strcasecmp(key, "debug")) {
			emd_debug = atoi(val);
		} else if (!strcasecmp(key, "port")) {
			emd_port = atoi(val);
		} else if (!strcasecmp(key, "dump")) {
			dump = atoi(val);
		} else if (!strcasecmp(key, "correct_time")) {
			correct_time = atoi(val);
		} else if (!strcasecmp(key, "second_divider")) {
			calc_second_divider = atoi(val);
		} else if (!strcasecmp(key, "sv_id_external_stream1")) {
			strncpy(streams_prop[0].sv_id, val, SV_ID_MAX_LEN - 1);
			streams_prop[0].sv_id[SV_ID_MAX_LEN - 1] = '\0';
		} else if (!strcasecmp(key, "sv_id_external_stream2")) {
			strncpy(streams_prop[1].sv_id, val, SV_ID_MAX_LEN - 1);
			streams_prop[1].sv_id[SV_ID_MAX_LEN - 1] = '\0';
		} else if (!strcasecmp(key, "src_mac_external_stream1")) {
			struct ether_addr *mac = ether_aton(val);
			if (mac)
				streams_prop[0].src_mac = *mac;
		} else if (!strcasecmp(key, "dst_mac_external_stream1")) {
			struct ether_addr *mac = ether_aton(val);
			if (mac)
				streams_prop[0].dst_mac = *mac;
		} else if (!strcasecmp(key, "src_mac_external_stream2")) {
			struct ether_addr *mac = ether_aton(val);
			if (mac)
				streams_prop[1].src_mac = *mac;
		} else if (!strcasecmp(key, "dst_mac_external_stream2")) {
			struct ether_addr *mac = ether_aton(val);
			if (mac)
				streams_prop[1].dst_mac = *mac;
		} else if (!strcasecmp(key, "sv_timeout_mks")) {
			sv_timeout_mks = atoi(val);
		} else if (!strcasecmp(key, "sv_threshold_mks")) {
			sv_threshold_mks = atoi(val);
		} else {
			emd_log(LOG_WARNING,
			    "unknown option '%s' in %s at line %d",
			    key, file, line);
			continue;
		}
	}	

	fclose(fp);

	return 0;
}

int set_streams_prop(streams_prop_resp *prop)
{
	if(emd_update_parameter(conffile, "src_mac_external_stream1", ether_ntoa(&prop->data[0].src_mac), "=") == -1 || 
		emd_update_parameter(conffile, "dst_mac_external_stream1", ether_ntoa(&prop->data[0].dst_mac), "=") == -1 ||
		emd_update_parameter(conffile, "sv_id_external_stream1", prop->data[0].sv_id, "=") == -1 ||

		emd_update_parameter(conffile, "src_mac_external_stream2", ether_ntoa(&prop->data[1].src_mac), "=") == -1 ||
		emd_update_parameter(conffile, "dst_mac_external_stream2", ether_ntoa(&prop->data[1].dst_mac), "=") == -1 ||
		emd_update_parameter(conffile, "sv_id_external_stream2", prop->data[1].sv_id, "=") == -1)
		return -1;

	memcpy(streams_prop, prop, sizeof(stream_property)*2);
	return 0;
}

#define USE_TMP_FILE
/* Обновляет значение параметра в файле конфигурации
 * @param conf_file: name of configuration file
 * @param par: key
 * @param new_value: value
 * @param sep: string of separators between key and value
 */
int emd_update_parameter(const char *conf_file, const char *par, const char *new_value, const char *sep)
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	int updated = 0;
	int ret = -1;
	char scan_fmt[64];
	size_t count;

#ifdef USE_TMP_FILE
	FILE *fp_tmp = tmpfile();
	
	if (fp_tmp == NULL) {
		emd_log(LOG_DEBUG, "tmpfile() failed: %s", strerror(errno));
		return -1;
	}
#else
	char *buf = strdup("");
	char *_buf = NULL;
#endif
	fp = fopen(conf_file, "r+");
	if (fp == NULL) {
		emd_log(LOG_ERR, "Не открыть файл(%s): %s", conf_file, strerror(errno));
		return -1;
	}

	snprintf(scan_fmt, sizeof(scan_fmt), "%%63[^%s\n]%s%%255[^\n]", sep, sep);

	while ((read = getline(&line, &len, fp)) != -1) {
		int n;
		char key[64], val[256];
		char *p = line;

		/* skip leading whitespace */
		while (*p && isspace((int)*p)) {
			p++;
		}
		/* blank lines and comments get ignored */
		if (*p && *p != '#') {
			n = sscanf(p, scan_fmt, key, val);
			if (n == 2 || n == 1) {
				if (strcmp(par, key) == 0) {
					// delete if new_value == null 
					if(new_value) {
#ifdef USE_TMP_FILE
						fprintf(fp_tmp, "%s%c%s\n", key, sep[0], new_value);
#else
						_buf = NULL;
						asprintf(&_buf, "%s%s%c%s\n", buf, key, sep[0], new_value);
						free(buf);
						buf = _buf;
#endif
					}
					updated = 1;
					continue;
				}
			}
		}
#ifdef USE_TMP_FILE
		fprintf(fp_tmp, "%s", line);
#else
		_buf = NULL;
		asprintf(&_buf,"%s%s", buf, line);
		free(buf);
		buf = _buf;
#endif
	}

	// add parament when one is absent
	if (!updated) {
		// new line
		char *nl = "";
		if (strlen(line) && !strchr(line, '\n'))
			nl = "\n";
#ifdef USE_TMP_FILE
		fprintf(fp_tmp, "%s%s%c%s\n", nl, par, sep[0], new_value);
#else
		_buf = NULL;
		asprintf(&_buf, "%s%s%s%c%s\n", buf, nl, par, sep[0], new_value);
		free(buf);
		buf = _buf;
#endif
	}
	
	rewind(fp);

#ifdef USE_TMP_FILE
	count = ftell(fp_tmp);
	rewind(fp_tmp);
	if (sendfile(fileno(fp), fileno(fp_tmp), NULL, count) == -1) {
		emd_log(LOG_DEBUG, "sendfile(%s, tmp) failed: %s", conf_file, strerror(errno));
		goto err;
	}
#else
	count = strlen(buf);	
	fprintf(fp, "%s", buf);
#endif
	ftruncate(fileno(fp), count);

	ret = 0;
err:	
	if (fp)
		fclose(fp);

	free(line);
#ifdef USE_TMP_FILE
	if (fp_tmp != NULL)
		fclose(fp_tmp);
#else
	free(buf);
#endif
	return ret;
}


