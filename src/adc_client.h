#ifndef ADC_CLIENT_H_
#define ADC_CLIENT_H_

#define BIT(x,i) ((x) & (0x1<<(i)))

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

#define U_RANGES_COUNT (U_RANGE_800 + 1) 
#define I_RANGES_COUNT (I_RANGE_100 + 1)

int adc_client_init();
int adc_client_close();

#include "proto.h"
#include "sv_read.h"

enum CALIB_TYPE {
	CALIB_TYPE_NULL,
	CALIB_TYPE_SCALE,
	CALIB_TYPE_ANGLE
};
#define CALIB_TYPES_COUNT (CALIB_TYPE_ANGLE + 1)

extern char adc_version[VERSION_MAX_LEN];
extern struct adc_properties adc_prop;
extern int adc_prop_valid;
extern float adc_coefs[CALIB_TYPES_COUNT][U_RANGES_COUNT*PHASES_IN_STREAM/2+I_RANGES_COUNT*PHASES_IN_STREAM/2];

int set_adc_prop(struct adc_properties *prop);
int set_adc_param(adc_param_req *param);
void adc_change_network(const char *addr, const char *mask);

#endif
