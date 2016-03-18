#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <string.h>

#include "emd.h"
#include "log.h"
#include "adc_tlv.h"
#include "settings.h"
#include "adc_client.h"

//#include "debug.h"

#define DEFAULT_ADC_PORT 1234
#define DEFAULT_ADC_IP4_ADDRESS "192.168.0.2"

#define RECV_BUF_SIZE 1512

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

static void *recv_buf;
static int sock = -1;
static uint16_t pcount;

static struct sockaddr_in
	local, remote;

static int read_properties();
static int adc_sock_send_recv(void *out, int out_len, void **in, int *in_len);
static int adc_send_recv(char out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len);

int adc_client_init()
{
	if (!adc_port)
		adc_port = DEFAULT_ADC_PORT;
	if (!adc_ip4_address)
		adc_ip4_address = strdup(DEFAULT_ADC_IP4_ADDRESS);

	adc_prop_valid = 0;

	recv_buf = malloc(RECV_BUF_SIZE);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		emd_log(LOG_DEBUG, "Error to open socket: %s", strerror(errno));
		return -1;
	}

	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(adc_port);

	memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_addr.s_addr = inet_addr(adc_ip4_address);
	remote.sin_port = htons(adc_port);

	if (bind(sock, (struct sockaddr *)&local, sizeof(local)) == -1) {
		emd_log(LOG_DEBUG, "Error to bind socket: %s", strerror(errno));
		return -1;
	}

	struct timespec tv = (struct timespec){1, 0};

	if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
		emd_log(LOG_DEBUG, "setsockopt(SO_SNDTIMEO) failed: %s", strerror(errno));
		return -1;
	}

	if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
	{
		emd_log(LOG_DEBUG, "setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
		return -1;
	}

	pcount = 0;
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
	char tag;
	char buf[32];
		
	// write ranges
	for (int i = 0; i < 8; i++) {
		if (adc_prop.range[i] != prop->range[i]) {
			req[0] = 0;
			req[1] = i;
			req[2] = prop->range[i];
			if ((ret = adc_send_recv(0xb0, req, 3, &tag, resp, sizeof(resp))) == -1)
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
		if ((ret = adc_send_recv(0xb4, req, 7, &tag, resp, sizeof(resp))) == -1)
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
		if ((ret = adc_send_recv(0xba, req, 7, &tag, resp, sizeof(resp))) == -1)
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
		if ((ret = adc_send_recv(0xb8, req, len + 1, &tag, resp, sizeof(resp))) == -1)
			goto err1;
		if (tag == 0x32 && resp[0] == 0x01) {
			strncpy(adc_prop.sv_id, prop->sv_id, SV_ID_MAX_LEN);
			adc_prop.sv_id[SV_ID_MAX_LEN] = '\0';
		} else
			goto err;
	}

	// rate
	if (adc_prop.rate != prop->rate) {
		req[0] = 0;
		req[1] = prop->rate;
		if ((ret = adc_send_recv(0xb2, req, 2, &tag, resp, sizeof(resp))) == -1)
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

int adc_send_recv(char out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len)
{
	int ret = -1;
	char *out_buf, *in_buf = NULL;
	int out_buf_len = out_len, in_buf_len = 0;
	uint8_t tag;
	uint16_t ccount;
	
	out_buf = malloc(out_len);
	memcpy(out_buf, out_value, out_len);

	if (encode(pcount++, out_tag, (uint8_t **)&out_buf, &out_buf_len) == -1) {
		emd_log(LOG_DEBUG, "encode() failed: %s", strerror(errno));	
		goto err;
	}

	if (adc_sock_send_recv(out_buf, out_buf_len, (void **)&in_buf, &in_buf_len) == -1) {
		goto err;
	}

	if (decode(&ccount, &tag, (uint8_t **)&in_buf, &in_buf_len) == -1) {
		emd_log(LOG_DEBUG, "decode failed: %s", strerror(errno));
		goto err;
	}

	ret = in_len < in_buf_len? in_len: in_buf_len;

	if (in_tag)
		*in_tag = tag;
	if (in_value) 
		memcpy(in_value, in_buf, ret);

err:
	if (out_buf)
		free(out_buf);
	if (in_buf)
		free(in_buf);

	return ret;
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
		if ((ret = adc_send_recv(0xb1, req, 2, &tag, resp, sizeof(resp))) == -1)
			return -1;

		if (tag == 0x32 && ret == 3)
			adc_prop.range[i] = resp[2];	
		else
			goto err;
	}

	// read macs
	req[0] = 0;
	if ((ret = adc_send_recv(0xbb, req, 1, &tag, resp, sizeof(resp))) == -1)
		return -1;
	if (tag == 0x32 && ret == 6) {
		char buf[32];
		snprintf(buf, sizeof(buf),  "%02X:%02X:%02X:%02X:%02X:%02X", 
			(uint8_t)resp[0], (uint8_t)resp[1], (uint8_t)resp[2], (uint8_t)resp[3], (uint8_t)resp[4], (uint8_t)resp[5]);
		memcpy(adc_prop.src_mac, buf, 17);
	} else
		goto err;

	if ((ret = adc_send_recv(0xb5, req, 1, &tag, resp, sizeof(resp))) == -1)
		return -1;
	if (tag == 0x32 && ret == 6) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", 
			(uint8_t)resp[0], (uint8_t)resp[1], (uint8_t)resp[2], (uint8_t)resp[3], (uint8_t)resp[4], (uint8_t)resp[5]);
		memcpy(adc_prop.dst_mac, buf, 17);
	} else
		goto err;


	// sv_id
	if ((ret = adc_send_recv(0xb9, req, 1, &tag, resp, sizeof(resp))) == -1)
		return -1;
	if (tag == 0x31) {
		ret = ret < SV_ID_MAX_LEN - 1? ret: SV_ID_MAX_LEN - 1;
		memcpy(adc_prop.sv_id, resp, ret);
		adc_prop.sv_id[ret] = '\0';
	} else
		goto err;
	
	// rate
	if ((ret = adc_send_recv(0xb3, req, 1, &tag, resp, sizeof(resp))) == -1)
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

int adc_sock_send_recv(void *out, int out_len, void **in, int *in_len)
{
	socklen_t sl;

	if (sendto(sock, out, out_len, 0, (const struct sockaddr *)&remote, sizeof(remote)) == -1) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "sendto failed:(%d) %s", errno, strerror(errno));
#endif
		return -1;
	}

	ssize_t sz;
	if ((sz = recvfrom(sock, recv_buf, RECV_BUF_SIZE, 0, NULL, &sl)) == -1) {
#ifdef DEBUG
		emd_log(LOG_WARNING,"recvfrom failed:(%d) %s", errno, strerror(errno));
#endif
		return -1;
	}

	if (in) {
		*in = malloc(sz);
		memcpy(*in, recv_buf, sz);
	}
	if (in_len)
		*in_len = sz;

	return 0;
}

int adc_client_close()
{
	adc_port = 0;
	if (adc_ip4_address) {
		free(adc_ip4_address);
		adc_ip4_address = NULL;
	}
	if (recv_buf) {
		free(recv_buf);
		recv_buf = NULL;
	}

	if (sock != -1) {
		close(sock);
		sock = -1;
	}

	return 0;
}

