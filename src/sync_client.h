#ifndef SYNC_CLIENT_H_
#define SYNC_CLIENT_H_

int sync_client_init();
int sync_client_close();

#include "proto.h"

extern char sync_version[VERSION_MAX_LEN];
extern struct sync_properties sync_prop;
extern int sync_prop_valid;

int set_sync_prop(struct sync_properties *prop);

void sync_change_network(const char *addr, const char *mask, const char *gw);
#endif
