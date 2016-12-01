#ifndef ADC_CLIENT_H_
#define ADC_CLIENT_H_

enum U_RANGE {
	U_RANGE_1 = 0,
	U_RANGE_2,
	U_RANGE_5,
	U_RANGE_10,
	U_RANGE_30,
	U_RANGE_60,
	U_RANGE_120,
	U_RANGE_240,
	U_RANGE_480,
	U_RANGE_800,
};

enum I_RANGE {
	I_RANGE_0d1 = 0,
	I_RANGE_0d25,
	I_RANGE_0d5,
	I_RANGE_1,
	I_RANGE_2d5,
	I_RANGE_5,
	I_RANGE_10,
	I_RANGE_25,
	I_RANGE_50,
	I_RANGE_100,
};

#define RANGE_NUM (U_RANGE_800 + 1) // or I_RANGE_100 + 1

int adc_client_init();
int adc_client_close();

#include "proto.h"

extern char adc_version[VERSION_MAX_LEN];
extern struct adc_properties adc_prop;
extern int adc_prop_valid;
extern float adc_coefs[3][RANGE_NUM][8];

int set_adc_prop(struct adc_properties *prop);
void adc_change_network(const char *addr, const char *mask);

#endif
