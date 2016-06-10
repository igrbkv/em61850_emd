#ifndef SYNC_CLIENT_H_
#define SYNC_CLIENT_H_

int sync_client_init();
int sync_client_close();

#include "proto.h"

extern struct sync_properties sync_prop;
extern int sync_prop_valid;

int set_sync_prop(struct sync_properties *prop);
#endif
