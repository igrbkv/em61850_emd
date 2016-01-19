#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include "emd.h"
#include "log.h"

#ifdef DEBUG
#include "debug.h"
#endif

#define DEFAULT_ADC_PORT 1234
#define DEFAULT_ADC_IP4_ADDRESS "192.168.0.2"

#define RECV_BUF_SIZE 1512

uint16_t adc_port;
char *adc_ip4_address; 
static void *recv_buf;
static int sock = -1;
//static uint16_t pcount;

static struct sockaddr_in
	local, remote;

int adc_client_init()
{
	if (!adc_port)
		adc_port = DEFAULT_ADC_PORT;
	if (!adc_ip4_address)
		adc_ip4_address = strdup(DEFAULT_ADC_IP4_ADDRESS);

	recv_buf = malloc(RECV_BUF_SIZE);

	sock = socket(
			AF_INET,
			SOCK_DGRAM, 0);
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
		emd_log(LOG_DEBUG, "setcockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
		return -1;
	}

	return 0;
}

int adc_client_send_recv(void *out, int out_len, void **in, int *in_len)
{
	socklen_t sl;

	if (sendto(sock, out, out_len, 0, (const struct sockaddr *)&remote, sizeof(remote)) == -1) {
#ifdef DEBUG
		int en = errno;
		emd_log(LOG_DEBUG, "sendto failed:(%d) %s", errno, strerror(errno));
		errno = en;
#endif
		return -1;
	}

	ssize_t sz;
	if ((sz = recvfrom(sock, recv_buf, RECV_BUF_SIZE, 0, NULL, &sl)) == -1) {
#ifdef DEBUG
		int en = errno;
		emd_log(LOG_WARNING,"recvfrom failed:(%d) %s", errno, strerror(errno));
		errno = en;
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

