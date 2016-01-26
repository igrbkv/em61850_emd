#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdint.h>

#define EMD_PORT 5025
#define SV_ID_MAX_LEN 64
extern unsigned short emd_port;

struct __attribute__((__packed__)) streams_properties {
	uint8_t stream1;
	char mac1[17];
	char sv_id1[SV_ID_MAX_LEN];
	uint32_t u_trans_coef1; 
	uint32_t i_trans_coef1; 
	uint8_t stream2;
	char mac2[17];
	char sv_id2[SV_ID_MAX_LEN];
	uint32_t u_trans_coef2; 
	uint32_t i_trans_coef2; 
};

extern struct streams_properties streams_prop; 

void set_default_settings();
int emd_read_conf(const char *file);
int set_streams_prop(struct streams_properties *prop);

#endif
