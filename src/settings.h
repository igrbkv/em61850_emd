#ifndef SETTINGS_H_
#define SETTINGS_H_

#define EMD_PORT 5025
extern unsigned short emd_port;

#include "proto.h"

extern struct stream_property streams_prop[2]; 

void set_default_settings();
int emd_read_conf(const char *file);
int emd_update_parameter(const char *conf_file, const char *par, const char *new_value, const char *sep);
int set_streams_prop(struct streams_properties *prop);
const char *get_distr_version();

extern int dump;
extern int correct_time;
extern unsigned short emd_pin_code;
#endif
