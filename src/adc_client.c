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
#include "tcp_server.h"


#define DEFAULT_ADC_PORT 1234

#define ADC_IDX(x) ((i+PHASES_IN_STREAM/2)%PHASES_IN_STREAM)
#define ARR_IDX(range, signal) (U_RANGES_COUNT*(PHASES_IN_STREAM/2)*((signal)/(PHASES_IN_STREAM/2)) + (range)*PHASES_IN_STREAM/2 + (signal)%(PHASES_IN_STREAM/2))

enum SV_DISCRETE {
	SV_DISCRETE_80 = 0,
	SV_DISCRETE_256,
	SV_DISCRETE_640,
	SV_DISCRETE_1280
};

struct adc_corrections adc_corrs;

char adc_version[VERSION_MAX_LEN];
uint16_t adc_port;
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

    char adc_ip4_addr[INET_ADDRSTRLEN];
    inc_ip4_addr(adc_ip4_addr, emd_ip4_addr, 1);
    emd_log(LOG_DEBUG, "adc board ip:%s", adc_ip4_addr);

	adc_prop_valid = 0;

    if (st.init(&st, adc_ip4_addr, adc_port) == -1)
        return -1;

	adc_prop_valid = (read_properties() != -1);
	emd_log(LOG_DEBUG, "adc read properties() %s", adc_prop_valid? "Succeed": "Failed");

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

	// write ranges
	for (int i = 0; i < 8; i++) {
		if (adc_prop.range[i] != prop->range[i]) {
            st.tag = 0xb0;
			st.value[0] = 0;
			st.value[1] = i;
			st.value[2] = prop->range[i];
            st.len = 3;
			if ((ret = st.send_recv(&st)) <= 0)
				return -1;

			if (st.tag == 0x32 && st.value[0] == 0x01)
				adc_prop.range[i] = prop->range[i];
			else
				goto err;
		}
	}

	// write macs
	if (memcmp(&adc_prop.dst_mac, &prop->dst_mac, sizeof(struct ether_addr)) != 0) {
        st.tag = 0xb4;
		st.value[0] = 0;
        memcpy(&st.value[1], &prop->dst_mac, sizeof(struct ether_addr));
        st.len = 7;
		if ((ret = st.send_recv(&st)) <= 0)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01)
			adc_prop.dst_mac = prop->dst_mac;
		else
			goto err;
	}
	if (memcmp(&adc_prop.src_mac, &prop->src_mac, sizeof(struct ether_addr)) != 0) {
        st.tag = 0xba;
		st.value[0] = 0;
		memcpy(&st.value[1], &prop->src_mac, sizeof(struct ether_addr));
        st.len = 7;
		if ((ret = st.send_recv(&st)) <= 0)
			goto err1;
		if (st.tag == 0x32 && st.value[0] == 0x01)
			adc_prop.src_mac = prop->src_mac;
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
		if ((ret = st.send_recv(&st)) <= 0)
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
		if ((ret = st.send_recv(&st)) <= 0)
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

int set_adc_param(adc_param_req *param)
{
	int ret;

    switch (param->type) {
        case ADC_PARAM_TYPE_RANGE: {
            for (int i = 0; i < PHASES_IN_STREAM; i++)
                if (BIT(param->stream_mask, i) && 
                    adc_prop.range[i] != param->range) {
                    st.tag = 0xb0;
                    st.value[0] = 0;
                    st.value[1] = ADC_IDX(i);
                    st.value[2] = param->range;
                    st.len = 3;
                    if ((ret = st.send_recv(&st)) <= 0)
                        return -1;

                    if (st.tag == 0x32 && st.value[0] == 0x01)
                        adc_prop.range[i] = param->range;
                    else
                        goto err;
                }
            break;  
		}
        case ADC_PARAM_TYPE_SRC_MAC: {
            if (memcmp(&adc_prop.src_mac, &param->mac, sizeof(struct ether_addr)) != 0) {
                st.tag = 0xba;
                st.value[0] = 0;
                memcpy(&st.value[1], &param->mac, sizeof(struct ether_addr));
                st.len = 7;
                if ((ret = st.send_recv(&st)) <= 0)
                    goto err1;
                if (st.tag == 0x32 && st.value[0] == 0x01)
                    adc_prop.src_mac = param->mac;
                else
                    goto err;
            }
            break;
        }
        case ADC_PARAM_TYPE_DST_MAC: {
            if (memcmp(&adc_prop.dst_mac, &param->mac, sizeof(struct ether_addr)) != 0) {
                st.tag = 0xb4;
                st.value[0] = 0;
                memcpy(&st.value[1], &param->mac, sizeof(struct ether_addr));
                st.len = 7;
                if ((ret = st.send_recv(&st)) <= 0)
                    goto err1;
                if (st.tag == 0x32 && st.value[0] == 0x01)
                    adc_prop.dst_mac = param->mac;
                else
                    goto err;
            }
            break;
        }
        case ADC_PARAM_TYPE_RATE: {
            if (adc_prop.rate != param->rate) {
                st.tag = 0xb2;
                st.value[0] = 0;
                st.value[1] = param->rate;
                st.len = 2;
                if ((ret = st.send_recv(&st)) <= 0)
                    goto err1;
                if (st.tag == 0x32 && st.value[0] == 0x01)
                    adc_prop.rate = param->rate;
                else
                    goto err;
            }
            break;
        }
        case ADC_PARAM_TYPE_SV_ID: {
            if (strncmp(adc_prop.sv_id, param->sv_id, SV_ID_MAX_LEN) != 0) {
                char buf[SV_ID_MAX_LEN * 2];
                strncpy(buf, param->sv_id, sizeof(buf));
                buf[sizeof(buf) - 1] = '\0';
                int len = strlen(buf);
                make_tlv(0x31, (uint8_t *)buf, &len, sizeof(buf));

                st.tag = 0xb8;
                st.value[0] = 0;
                // FIXME len > sizeof(re) - 1
                memcpy(&st.value[1], buf, len);
                st.len = len + 1;
                if ((ret = st.send_recv(&st)) <= 0)
                    goto err1;
                if (st.tag == 0x32 && st.value[0] == 0x01) {
                    strncpy(adc_prop.sv_id, param->sv_id, SV_ID_MAX_LEN);
                    adc_prop.sv_id[SV_ID_MAX_LEN-1] = '\0';
                } else
                    goto err;
            }
            break;
        }
        case ADC_PARAM_TYPE_CALIB_NULL: {
            for (int i = 0; i < PHASES_IN_STREAM; i++)
                if (BIT(param->stream_mask, i)) {
                    st.tag = 0xA2;
                    st.value[0] = 0;
                    st.value[1] = 0;
                    st.value[2] = ADC_IDX(i);
                    st.value[3] = param->range;
                    uint32_t v = *((uint32_t *)&param->null[i]);
                    v = htonl(v);
                    memcpy(&st.value[4], &v, sizeof(v));
                    st.len = 8;
                    if ((ret = st.send_recv(&st)) <= 0)
                        return -1;

                    if (st.tag == 0x32 && st.value[0] == 4)
                        adc_corrs.null[ARR_IDX(param->range, ADC_IDX(i))] = param->null[i];
                    else
                        goto err;
                }
            break;  
		}
        case ADC_PARAM_TYPE_CALIB_SCALE: {
            for (int i = 0; i < PHASES_IN_STREAM; i++)
                if (BIT(param->stream_mask, i)) {
                    st.tag = 0xA2;
                    st.value[0] = 0;
                    st.value[1] = 1;
                    st.value[2] = ADC_IDX(i);
                    st.value[3] = param->range;
                    uint32_t v = *((uint32_t *)&param->scale[i]);
                    v = htonl(v);
                    memcpy(&st.value[4], &v, sizeof(v));
                    st.len = 8;
                    if ((ret = st.send_recv(&st)) <= 0)
                        return -1;

                    if (st.tag == 0x32 && st.value[0] == 4)
                        adc_corrs.scale[ARR_IDX(param->range, ADC_IDX(i))] = param->scale[i];
                    else
                        goto err;
                }
            break;  
		}
        case ADC_PARAM_TYPE_CALIB_SHIFT: {
            for (int i = 0; i < PHASES_IN_STREAM; i++)
                if (BIT(param->stream_mask, i)) {
                    st.tag = 0xA6;
                    st.value[0] = 0;
                    st.value[1] = ADC_IDX(i);
                    st.value[2] = param->range;
                    uint32_t v = *((uint32_t *)&param->shift[i]);
                    v = htonl(v);
                    memcpy(&st.value[3], &v, sizeof(v));
                    st.len = 7;
                    if ((ret = st.send_recv(&st)) <= 0)
                        return -1;

                    if (st.tag == 0x32 && st.value[0] == 4)
                        adc_corrs.shift[ARR_IDX(param->range, ADC_IDX(i))] = param->shift[i];
                    else
                        goto err;
                }
            break;  
		}

    }
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
		if ((ret = st.send_recv(&st)) <= 0)
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
	if ((ret = st.send_recv(&st)) <= 0)
        return -1;
	if (st.tag == 0x32 && st.len == 6) {
        memcpy(&adc_prop.src_mac, st.value, sizeof(struct ether_addr));
	} else
        goto err;

    st.tag = 0xb5;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) <= 0)
        return -1;
	if (st.tag == 0x32 && st.len == 6) {
        memcpy(&adc_prop.dst_mac, st.value, sizeof(struct ether_addr));
	} else
        goto err;

	// sv_id
    st.tag = 0xb9;
	st.value[0] = 0;
    st.len = 1;
	if ((ret = st.send_recv(&st)) <= 0)
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
	if ((ret = st.send_recv(&st)) <= 0)
        return -1;
	if (st.tag == 0x32 && ret == 2)
        adc_prop.rate = st.value[1];
	else
        goto err;

    // version
    st.tag = 0xf0;
    st.len = 0;
    if ((ret = st.send_recv(&st)) <= 0)
        return -1;
    if (st.tag == 0xf0 && ret == 12) 
        snprintf(adc_version, sizeof(adc_version), "%u.%u.%u (%u)", 
            *((uint8_t *)&st.value[0]),
            *((uint8_t *)&st.value[1]),
            *((uint8_t *)&st.value[2]),
            *((uint32_t *)&st.value[4]));
	else
        goto err;

    // coefs
    // null
    for (int j = 0; j < U_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa3;
            st.value[0] = 0x0;
            st.value[1] = 0;
            st.value[2] = k;
            st.value[3] = j;
            st.len = 4;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);
                adc_corrs.null[ARR_IDX(j, k)] = *(int *)&v;
            } else
                goto err;
        }
    for (int j = 0; j < I_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa3;
            st.value[0] = 0x0;
            st.value[1] = 0;
            st.value[2] = k + 4;
            st.value[3] = j;
            st.len = 4;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);

                adc_corrs.null[ARR_IDX(j, k+4)] = *(int *)&v;
            }
        }

    // scale
    for (int j = 0; j < U_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa3;
            st.value[0] = 0x0;
            st.value[1] = 1;
            st.value[2] = k;
            st.value[3] = j;
            st.len = 4;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);
                adc_corrs.scale[ARR_IDX(j, k)] = *(float *)&v;
            } else
                goto err;
        }
    for (int j = 0; j < I_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa3;
            st.value[0] = 0x0;
            st.value[1] = 1;
            st.value[2] = k + 4;
            st.value[3] = j;
            st.len = 4;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);

                adc_corrs.scale[ARR_IDX(j, k+4)] = *(float *)&v;
            }
        }

    // shift
    for (int j = 0; j < U_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa7;
            st.value[0] = 0x0;
            st.value[1] = k;
            st.value[2] = j;
            st.len = 3;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);
                adc_corrs.shift[ARR_IDX(j, k)] = *(float *)&v;
            } else
                goto err;
        }
    for (int j = 0; j < I_RANGES_COUNT; j++)    
        for (int k = 0; k < 4; k++) {
            st.tag = 0xa7;
            st.value[0] = 0x0;
            st.value[1] = k + 4;
            st.value[2] = j;
            st.len = 3;
            if (st.send_recv(&st) <= 0)
                return -1;
            if (st.tag == 0x32 && st.len == 4) {
                uint32_t v = ntohl(*(uint32_t *)&st.value[0]);
                adc_corrs.shift[ARR_IDX(j, k + 4)] = *(float *)&v;
            } else
                goto err;
        }

	return 0;
err:
	emd_log(LOG_DEBUG, "adc request rejected");
	return -1;
}

void adc_change_network(const char *addr, const char *mask)
{
    // "255.255.255.0" ==> 24
    char buf[INET_ADDRSTRLEN];
    strcpy(buf, mask);
    char *tok, *ptr = buf;
    int n = 0, i = 0;
    while ((tok = strsep(&ptr, "."))) {
        int v = atoi(tok) & 0xff;
        for (; i < 8; i++)
            if (v & (0x1 << (7-i)))
                n++;
    }

    st.tag = 0xc0;
    st.len = 6;
    st.value[0] = 0;
    strcpy(buf, addr);
    ptr = buf;
    i = 1;
    while ((tok = strsep(&ptr, ".")))
        st.value[i++] = atoi(tok) & 0xff;
    st.value[5] = n;

    st.send_recv(&st);
}

int adc_client_close()
{
	adc_port = 0;

    st.close(&st);

	return 0;
}

