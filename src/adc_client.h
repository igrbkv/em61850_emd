#ifndef ADC_CLIENT_H_
#define ADC_CLIENT_H_

int adc_client_init();
int adc_client_close();

#include "proto.h"

extern char adc_version[VERSION_MAX_LEN];
extern struct adc_properties adc_prop;
extern int adc_prop_valid;

int set_adc_prop(struct adc_properties *prop);
void adc_change_network(const char *addr, const char *mask);

#endif
