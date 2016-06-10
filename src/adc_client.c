#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <string.h>

#include "emd.h"
#include "log.h"
#include "tlv.h"
#include "settings.h"
#include "adc_client.h"
#include "sock_tlv.h"


#define DEFAULT_ADC_PORT 1234
#define DEFAULT_ADC_IP4_ADDRESS "192.168.0.2"


enum SV_DISCRETE {
	SV_DISCRETE_80 = 0,
	SV_DISCRETE_256,
	SV_DISCRETE_640,
	SV_DISCRETE_1280
};

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
	U_RANGE_INVALID
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
	I_RANGE_INVALID
};

uint16_t adc_port;
char *adc_ip4_address;
struct adc_properties adc_prop;
int adc_prop_valid = 0;

static struct sock_tlv st = {
    .init = &sock_tlv_init,
    .send_recv = &sock_tlv_send_recv,
    .close = &sock_tlv_close
};


static int read_properties();

int adc_client_init()
{
	if (!adc_port)
		adc_port = DEFAULT_ADC_PORT;
	if (!adc_ip4_address)
		adc_ip4_address = strdup(DEFAULT_ADC_IP4_ADDRESS);


	adc_prop_valid = 0;

    if (st.init(&st, adc_ip4_address, adc_port) == -1)
        return -1;

	adc_prop_valid = (read_properties() != -1);

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


int set_adc_prop(struct adc_properties *prop)
{
	int ret;
	char req[16];
	char resp[256];
	uint8_t tag;
	char buf[32];

	// write ranges
	for (int i = 0; i < 8; i++) {
		if (adc_prop.range[i] != prop->range[i]) {
			req[0] = 0;
			req[1] = i;
			req[2] = prop->range[i];
			if ((ret = st.send_recv(&st, 0xb0, req, 3, &tag, resp, sizeof(resp))) == -1)
				return -1;

			if (tag == 0x32 && resp[0] == 0x01)
				adc_prop.range[i] = prop->range[i];
			else
				goto err;
		}
	}

	// write macs
	if (memcmp(adc_prop.dst_mac, prop->dst_mac, 17) != 0) {
		req[0] = 0;
		memcpy(buf, prop->dst_mac, 17);
		buf[17] = '\0';
		sscanf(buf, "%x:%x:%x:%x:%x:%x",
			&req[1], &req[2], &req[3],
			&req[4], &req[5], &req[6]);
		if ((ret = st.send_recv(&st, 0xb4, req, 7, &tag, resp, sizeof(resp))) == -1)
			goto err1;
		if (tag == 0x32 && resp[0] == 0x01)
			memcpy(adc_prop.dst_mac, prop->dst_mac, 17);
		else
			goto err;
	}
	if (memcmp(adc_prop.src_mac, prop->src_mac, 17) != 0) {
		req[0] = 0;
		memcpy(buf, prop->src_mac, 17);
		buf[17] = '\0';
		sscanf(buf, "%x:%x:%x:%x:%x:%x",
			&req[1], &req[2], &req[3],
			&req[4], &req[5], &req[6]);
		if ((ret = st.send_recv(&st, 0xba, req, 7, &tag, resp, sizeof(resp))) == -1)
			goto err1;
		if (tag == 0x32 && resp[0] == 0x01)
			memcpy(adc_prop.src_mac, prop->src_mac, 17);
		else
			goto err;
	}

	// write sv_id
	if (strncmp(adc_prop.sv_id, prop->sv_id, SV_ID_MAX_LEN) != 0) {
		char *buf = strdup(prop->sv_id);
		int len = strlen(buf);
		make_tlv(0x31, (uint8_t **)&buf, &len);

		req[0] = 0;
		// FIXME len > sizeof(re) - 1
		memcpy(&req[1], buf, len);
		free(buf);
		if ((ret = st.send_recv(&st, 0xb8, req, len + 1, &tag, resp, sizeof(resp))) == -1)
			goto err1;
		if (tag == 0x32 && resp[0] == 0x01) {
			strncpy(adc_prop.sv_id, prop->sv_id, SV_ID_MAX_LEN);
			adc_prop.sv_id[SV_ID_MAX_LEN-1] = '\0';
		} else
			goto err;
	}

	// rate
	if (adc_prop.rate != prop->rate) {
		req[0] = 0;
		req[1] = prop->rate;
		if ((ret = st.send_recv(&st, 0xb2, req, 2, &tag, resp, sizeof(resp))) == -1)
			goto err1;
		if (tag == 0x32 && resp[0] == 0x01)
			adc_prop.rate = prop->rate;
		else
			goto err;
	}

	adc_prop_valid = 1;
	return 0;

err:
	emd_log(LOG_DEBUG, "adc request rejected");
err1:
	adc_prop_valid = 0;
	return -1;

}

int read_properties()
{
	int ret;
	char req[16];
	char resp[256];
	char tag;

	// read ranges
	for (int i = 0; i < 8; i++) {
		req[0] = 0;
		req[1] = i;
		if ((ret = st.send_recv(&st, 0xb1, req, 2, &tag, resp, sizeof(resp))) == -1)
			return -1;

		if (tag == 0x32 && ret == 3)
			adc_prop.range[i] = resp[2];
		else
			goto err;
	}

	// read macs
	req[0] = 0;
	if ((ret = st.send_recv(&st, 0xbb, req, 1, &tag, resp, sizeof(resp))) == -1)
        return -1;
	if (tag == 0x32 && ret == 6) {
        char buf[32];
        snprintf(buf, sizeof(buf),  "%02X:%02X:%02X:%02X:%02X:%02X",
                (uint8_t)resp[0], (uint8_t)resp[1], (uint8_t)resp[2], (uint8_t)resp[3], (uint8_t)resp[4], (uint8_t)resp[5]);
        memcpy(adc_prop.src_mac, buf, 17);
	} else
        goto err;

	if ((ret = st.send_recv(&st, 0xb5, req, 1, &tag, resp, sizeof(resp))) == -1)
        return -1;
	if (tag == 0x32 && ret == 6) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                (uint8_t)resp[0], (uint8_t)resp[1], (uint8_t)resp[2], (uint8_t)resp[3], (uint8_t)resp[4], (uint8_t)resp[5]);
        memcpy(adc_prop.dst_mac, buf, 17);
	} else
        goto err;


	// sv_id
	if ((ret = st.send_recv(&st, 0xb9, req, 1, &tag, resp, sizeof(resp))) == -1)
        return -1;
	if (tag == 0x31) {
        ret = ret < SV_ID_MAX_LEN - 1? ret: SV_ID_MAX_LEN - 1;
        memcpy(adc_prop.sv_id, resp, ret);
        adc_prop.sv_id[ret] = '\0';
	} else
        goto err;

	// rate
	if ((ret = st.send_recv(&st, 0xb3, req, 1, &tag, resp, sizeof(resp))) == -1)
        return -1;
	if (tag == 0x32 && ret == 2)
        adc_prop.rate = resp[1];
	else
        goto err;

	return 0;
err:
	emd_log(LOG_DEBUG, "adc request rejected");
	return -1;
}

int adc_client_close()
{
	adc_port = 0;
	if (adc_ip4_address) {
            free(adc_ip4_address);
            adc_ip4_address = NULL;
	}

    st.close(&st);

	return 0;
}

