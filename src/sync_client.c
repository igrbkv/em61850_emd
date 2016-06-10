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


#define DEFAULT_SYNC_PORT 1234
#define DEFAULT_SYNC_IP4_ADDRESS "192.168.0.3"


uint16_t sync_port;
char *sync_ip4_address;
struct sync_properties sync_prop;
int sync_prop_valid = 0;

static struct sock_tlv st = {
    .init = &sock_tlv_init,
    .send_recv = &sock_tlv_send_recv,
    .close = &sock_tlv_close
};

static int read_properties();

int sync_client_init()
{
	if (!sync_port)
		sync_port = DEFAULT_SYNC_PORT;
	if (!sync_ip4_address)
		sync_ip4_address = strdup(DEFAULT_SYNC_IP4_ADDRESS);

	sync_prop_valid = 0;

	emd_log(LOG_DEBUG, "sync init()");
    if (st.init(&st, sync_ip4_address, sync_port) == -1)
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
	char req[32];
	char resp[256];
	uint8_t tag;

	// input
	req[0] = prop->in_sig;
	if ((ret = st.send_recv(&st, 0xc8, req, 1, &tag, resp, sizeof(resp))) == -1)
		goto err;

	if (tag == 0x32 && resp[0] == 0x01)
		sync_prop.in_sig = prop->in_sig;
	else 
		goto err1;

	// PPS out (Output 3) CLK out (Output 4)
	uint8_t cmd[] = {0xa5, 0xa7};
	for (int i = 0; i < sizeof(cmd); i++) {
		if ((ret = st.send_recv(&st, cmd[i], (void *)&prop->out[i], sizeof(struct output_properties), &tag, resp, sizeof(resp))) == -1)
			goto err1;

		if (tag == 0x32 && resp[0] == 0x01)
			sync_prop.out[i] = prop->out[i];
		else
			goto err;
	}

	sync_prop_valid = 1;
	return 0;

err:
	emd_log(LOG_DEBUG, "sync request rejected");
err1:
	sync_prop_valid = 0;
	return -1;
}

int read_properties()
{
	int ret;
	char resp[256];
	uint8_t tag;

	// input
	if ((ret = st.send_recv(&st, 0xc7, NULL, 0, &tag, resp, sizeof(resp))) == -1)
		goto err;
		
	if (tag == 0xc7) 
		sync_prop.in_sig = resp[0];
	else
		goto err;

	// PPS out (Output 3) CLK out (Output 4)
	uint8_t cmd[] = {0xa4, 0xa6};
	for (int i = 0; i < sizeof(cmd); i++) {
		if ((ret = st.send_recv(&st, cmd[i], NULL, 0, &tag, resp, sizeof(resp))) == -1)
			goto err;
			
		if (tag == cmd[i])
			sync_prop.out[i] = *(struct output_properties *)&resp[0];
		else
			goto err;
	}

	return 0;
err:
	emd_log(LOG_DEBUG, "sync request failed");

	return -1;
}

int sync_client_close()
{
	sync_port = 0;
	if (sync_ip4_address) {
            free(sync_ip4_address);
            sync_ip4_address = NULL;
	}

    st.close(&st);

	return 0;
}

