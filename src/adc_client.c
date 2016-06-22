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
    .close = &sock_tlv_close,
    .timeout = 500
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
    char buf[32];

	// write ranges
	for (int i = 0; i < 8; i++) {
		if (adc_prop.range[i] != prop->range[i]) {
            st.tag = 0xb0;
			st.value[0] = 0;
			st.value[1] = i;
			st.value[2] = prop->range[i];
            st.len = 3;
			if ((ret = st.send_recv(&st)) == -1)
				return -1;

			if (st.tag == 0x32 && st.value[0] == 0x01)
				adc_prop.range[i] = prop->range[i];
			else
				goto err;
		}
	}

	// write macs
	if (memcmp(adc_prop.dst_mac, prop->dst_mac, 17) != 0) {
        st.tag = 0xb4;
		st.value[0] = 0;
		memcpy(buf, prop->dst_mac, 17);
		buf[17] = '\0';
		sscanf(buf, "%x:%x:%x:%x:%x:%x",
			&st.value[1], &st.value[2], &st.value[3],
			&st.value[4], &st.value[5], &st.value[6]);
        st.len = 7;
		if ((ret = st.send_recv(&st)) == -1)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01)
			memcpy(adc_prop.dst_mac, prop->dst_mac, 17);
		else
			goto err;
	}
	if (memcmp(adc_prop.src_mac, prop->src_mac, 17) != 0) {
        st.tag = 0xba;
		st.value[0] = 0;
		memcpy(buf, prop->src_mac, 17);
		buf[17] = '\0';
		sscanf(buf, "%x:%x:%x:%x:%x:%x",
			&st.value[1], &st.value[2], &st.value[3],
			&st.value[4], &st.value[5], &st.value[6]);
        st.len = 7;
		if ((ret = st.send_recv(&st)) == -1)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01)
			memcpy(adc_prop.src_mac, prop->src_mac, 17);
		else
			goto err;
	}

	// write sv_id
	if (strncmp(adc_prop.sv_id, prop->sv_id, SV_ID_MAX_LEN) != 0) {
		char buf[SV_ID_MAX_LEN * 2];
        strncpy(buf, prop->sv_id, sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
		int len = strlen(buf);
		make_tlv(0x31, (uint8_t *)buf, &len, sizeof(buf));

        st.tag = 0xb8;
		st.value[0] = 0;
		// FIXME len > sizeof(re) - 1
		memcpy(&st.value[1], buf, len);
        st.len = len + 1;
		if ((ret = st.send_recv(&st)) == -1)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01) {
			strncpy(adc_prop.sv_id, prop->sv_id, SV_ID_MAX_LEN);
			adc_prop.sv_id[SV_ID_MAX_LEN-1] = '\0';
		} else
			goto err;
	}

	// rate
	if (adc_prop.rate != prop->rate) {
        st.tag = 0xb2;
		st.value[0] = 0;
		st.value[1] = prop->rate;
        st.len = 2;
		if ((ret = st.send_recv(&st)) == -1)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01)
			adc_prop.rate = prop->rate;
		else
			goto err;
	}

	adc_prop_valid = 1;
	return 0;

err:
	emd_log(LOG_DEBUG, "adc request rejected");
err1:
	return -1;

}

int read_properties()
{
	int ret;

	// read ranges
	for (int i = 0; i < 8; i++) {
        st.tag = 0xb1;
		st.value[0] = 0;
		st.value[1] = i;
        st.len = 2;
		if ((ret = st.send_recv(&st)) == -1)
			return -1;

		if (st.tag == 0x32 && st.len == 3)
			adc_prop.range[i] = st.value[2];
		else
			goto err;
	}

	// read macs
    st.tag = 0xbb;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) == -1)
        return -1;
	if (st.tag == 0x32 && st.len == 6) {
        char buf[32];
        snprintf(buf, sizeof(buf),  "%02X:%02X:%02X:%02X:%02X:%02X",
                (uint8_t)st.value[0], (uint8_t)st.value[1], (uint8_t)st.value[2], (uint8_t)st.value[3], (uint8_t)st.value[4], (uint8_t)st.value[5]);
        memcpy(adc_prop.src_mac, buf, 17);
	} else
        goto err;

    st.tag = 0xb5;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) == -1)
        return -1;
	if (st.tag == 0x32 && st.len == 6) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                (uint8_t)st.value[0], (uint8_t)st.value[1], (uint8_t)st.value[2], (uint8_t)st.value[3], (uint8_t)st.value[4], (uint8_t)st.value[5]);
        memcpy(adc_prop.dst_mac, buf, 17);
	} else
        goto err;

	// sv_id
    st.tag = 0xb9;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) == -1)
        return -1;
	if (st.tag == 0x31) {
        ret = ret < SV_ID_MAX_LEN - 1? ret: SV_ID_MAX_LEN - 1;
        memcpy(adc_prop.sv_id, st.value, ret);
        adc_prop.sv_id[ret] = '\0';
	} else
        goto err;

	// rate
    st.tag = 0xb3;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) == -1)
        return -1;
	if (st.tag == 0x32 && ret == 2)
        adc_prop.rate = st.value[1];
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

