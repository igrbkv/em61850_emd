#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

#include "emd.h"
#include "log.h"
#include "tlv.h"
#include "settings.h"
#include "sync_client.h"
#include "sock_tlv.h"
#include "tcp_server.h"


#define DEFAULT_SYNC_PORT 1234

char sync_version[VERSION_MAX_LEN];
uint16_t sync_port;
struct sync_properties sync_prop;
int sync_prop_valid = 0;

static struct sock_tlv st = {
    .init = &sock_tlv_init,
    .send_recv = &sock_tlv_send_recv,
    .close = &sock_tlv_close,
	.poll = &sock_tlv_poll,
	.timeout = 250
};

static int read_properties();

int sync_client_init()
{
	if (!sync_port)
		sync_port = DEFAULT_SYNC_PORT;

	char sync_ip4_addr[INET_ADDRSTRLEN];
	inc_ip4_addr(sync_ip4_addr, emd_ip4_addr, 2);
    emd_log(LOG_DEBUG, "sync board ip:%s", sync_ip4_addr);

	sync_prop_valid = 0;

	emd_log(LOG_DEBUG, "sync init()");
    if (st.init(&st, sync_ip4_addr, sync_port) == -1)
        return -1;

	sync_prop_valid = (read_properties() != -1);
	emd_log(LOG_DEBUG, "sync read properties() %s", sync_prop_valid? "Succeed": "Failed");

	return 0;
}

#ifdef DEBUG
char *print_packet(uint8_t *pack, int len)
{
	static char buf[256] = {0};
	int n = 0;
	for (int i = 0; i < len; i++)
		n += snprintf(&buf[n], 256-n, "%02x", pack[i]);
	return buf;
}
#endif


int set_sync_prop(struct sync_properties *prop)
{
	int ret;

	// input
	st.tag = 0xc8;
	st.value[0] = prop->in_sig;
	st.len = 1;

	emd_log(LOG_DEBUG, "send tag: 0x%02X", st.tag);
	if ((ret = st.send_recv(&st)) <= 0) {
err1:
		emd_log(LOG_DEBUG, "unknown reply");
		goto end;
	}

	if (st.tag == 0x32 && st.value[0] == 0x01)
		sync_prop.in_sig = prop->in_sig;
	else {
err:
		emd_log(LOG_DEBUG, "sync board rejected request: received tag: 0x%02X", st.tag);
		goto end;
	}

	// PPS out (Output 3) CLK out (Output 4)
	uint8_t cmd[] = {0xa5, 0xa7};
	for (int i = 0; i < sizeof(cmd); i++) {
		st.tag = cmd[i];
		memcpy(st.value, &prop->out[i], sizeof(struct output_properties));
		st.len = sizeof(struct output_properties);
		emd_log(LOG_DEBUG, "send tag: 0x%02X", st.tag);
		if ((ret = st.send_recv(&st)) <= 0)
			goto err1;

		if (st.tag == 0x32 && st.value[0] == 0x01)
			sync_prop.out[i] = prop->out[i];
		else
			goto err;
	}

	sync_prop_valid = 1;
	return 0;

end:
	return -1;
}

int read_properties()
{
	int ret;

	// input
	st.tag = 0xc7;
	st.len = 0;
	if ((ret = st.send_recv(&st)) <= 0)
		goto err1;
		
	if (st.tag == 0xc7) 
		sync_prop.in_sig = st.value[0];
	else {
err:
		emd_log(LOG_DEBUG, "sync board rejected request");
		goto err1;
	}

	// PPS out (Output 3) CLK out (Output 4)
	uint8_t cmd[] = {0xa4, 0xa6};
	for (int i = 0; i < sizeof(cmd); i++) {
		st.tag = cmd[i];
		st.len = 0;
		if ((ret = st.send_recv(&st)) <= 0)
			goto err1;
			
		if (st.tag == cmd[i])
			sync_prop.out[i] = *(struct output_properties *)&st.value[0];
		else
			goto err;
	}

    // version
    st.tag = 0xf0;
    st.len = 0;
    if ((ret = st.send_recv(&st)) <= 0)
        return -1;
	emd_log(LOG_DEBUG, "return:%02X %d", st.tag, ret);
    if (st.tag == 0xf0 && ret >= 10) 
        snprintf(sync_version, sizeof(sync_version), "%u.%u.%u (%u)", 
            *((uint8_t *)&st.value[2]),
            *((uint8_t *)&st.value[3]),
            *((uint8_t *)&st.value[4]),
            *((uint32_t *)&st.value[5]));
	else
        goto err;

	return 0;
err1:
	return -1;
}

void sync_change_network(const char *addr, const char *mask, const char *gw)
{
    char buf[INET_ADDRSTRLEN];
    st.tag = 0xb7;
    st.len = 12;

    strcpy(buf, addr);
    char *tok, *ptr = buf;
    int i = 0;
    while ((tok = strsep(&ptr, ".")))
        st.value[i++] = atoi(tok);
	
    strcpy(buf, mask);
    ptr = buf;
	i = 4;
    while ((tok = strsep(&ptr, ".")))
        st.value[i++] = atoi(tok);

    strcpy(buf, gw);
    ptr = buf;
	i = 8;
    while ((tok = strsep(&ptr, ".")))
        st.value[i++] = atoi(tok);

    st.send_recv(&st);

	// reset
	st.tag = 0xf3;
	st.len = 1;
	st.value[0] = 0;

    st.send_recv(&st);
}

int sync_client_close()
{
	sync_port = 0;

    st.close(&st);

	return 0;
}

