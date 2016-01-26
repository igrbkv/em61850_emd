#ifndef ADC_CLIENT_H_
#define ADC_CLIENT_H_

int adc_client_init();
int adc_client_close();

#include <stdint.h>

#include "settings.h"

struct __attribute__((__packed__)) adc_properties {
	union {
		struct {
			uint8_t ua_range;
			uint8_t ub_range;
			uint8_t uc_range;
			uint8_t un_range;
			uint8_t ia_range;
			uint8_t ib_range;
			uint8_t ic_range;
			uint8_t in_range;
		};
		uint8_t range[8]; // enum U_RANGE or I_RANGE
	};
	char src_mac[17];
	char dst_mac[17];
	uint8_t rate;	// enum SV_DISCRETE 
	char sv_id[SV_ID_MAX_LEN];
};

int set_adc_prop(struct adc_properties *prop);

extern struct adc_properties adc_prop;
extern int adc_prop_valid;
#endif
