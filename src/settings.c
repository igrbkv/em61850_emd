#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <sys/types.h>

#include "log.h"
#include "settings.h"

/* Настройки энергомонитора:
 * emd_external_stream1 = 0 - (по умолчанию) 
 *			используется ADC
 *						  1 - используется внешний 
 *			поток 1
 *		emd_mac_external_stream1 - mac внешнего потока 1
 *		emd_sv_id_external_stream1 - sv_id внешнего 
 *			потока 1
 * emd_external_stream2 = 0 - (по умолчанию) внешний 
 *						поток 2 не используется
 *						  1 - внешний поток 2 
 *			используется 
 *		emd_mac_external_stream2 - mac внешнего потока 2
 *		emd_sv_id_external_stream2 - sv_id внешнего потока 2
 * emd_X_trans_coef_streamN = 1 - по умолчанию
 */

unsigned short emd_port;
struct streams_properties streams_prop; 

void set_default_settings()
{
	emd_port = EMD_PORT;
	streams_prop.stream1 = 0;
	streams_prop.mac1[0] = '\0';
	streams_prop.sv_id1[0] = '\0';
	streams_prop.u_trans_coef1 = 1; 
	streams_prop.i_trans_coef1 = 1; 
	streams_prop.stream2 = 0;
	streams_prop.mac2[0] = '\0';
	streams_prop.sv_id2[0] = '\0';
	streams_prop.u_trans_coef2 = 1; 
	streams_prop.i_trans_coef2 = 1;
}

int emd_read_conf(const char *file)
{
	FILE *fp;
	char buf[512];
	int line = 0;

	emd_log(LOG_DEBUG, "parsing conf file %s", file);

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
		} else if (!strcasecmp(key, "external_stream1")) {
			streams_prop.stream1 = atoi(val);
		} else if (!strcasecmp(key, "external_stream2")) {
			streams_prop.stream2 = atoi(val);
		} else if (!strcasecmp(key, "sv_id_external_stream1")) {
			strncpy(streams_prop.sv_id1, val, SV_ID_MAX_LEN - 1);
			streams_prop.sv_id1[SV_ID_MAX_LEN - 1] = '\0';
		} else if (!strcasecmp(key, "sv_id_external_stream2")) {
			strncpy(streams_prop.sv_id2, val, SV_ID_MAX_LEN - 1);
			streams_prop.sv_id2[SV_ID_MAX_LEN - 1] = '\0';
		} else if (!strcasecmp(key, "mac_external_stream1")) {
			strncpy(streams_prop.mac1, val, 17 - 1);
			streams_prop.mac1[17 - 1] = '\0';
		} else if (!strcasecmp(key, "mac_external_stream2")) {
			strncpy(streams_prop.mac2, val, 17 - 1);
			streams_prop.mac2[17 - 1] = '\0';
		} else if (!strcasecmp(key, "u_trans_coef_stream1")) {
			streams_prop.u_trans_coef1 = atoi(val);
		} else if (!strcasecmp(key, "i_trans_coef_stream1")) {
			streams_prop.i_trans_coef1 = atoi(val);
		} else if (!strcasecmp(key, "u_trans_coef_stream2")) {
			streams_prop.u_trans_coef2 = atoi(val);
		} else if (!strcasecmp(key, "i_trans_coef_stream1")) {
			streams_prop.i_trans_coef2 = atoi(val);
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

int set_streams_prop(struct streams_properties *prop)
{
	if (streams_prop.stream1 != prop->stream1) {
		
	}
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
						asprintf(&buf, "%s%s%c%s\n", buf, key, sep[0], new_value);
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
		asprintf(&buf,"%s%s", buf, line);
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
		asprintf(&buf, "%s%s%s%c%s\n", buf, nl, par, sep[0], new_value);
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


