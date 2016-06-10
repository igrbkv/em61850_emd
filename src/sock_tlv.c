#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_alg.h>
#include <arpa/inet.h>

#include "log.h"
#include "tlv.h"
#include "sock_tlv.h"

#define RECV_BUF_SIZE 1512
#define SEND_RECV_RETRY 10 


int sock_tlv_init(struct sock_tlv *st, const char *remote, uint16_t port)
{
	st->recv_buf = malloc(RECV_BUF_SIZE);

	st->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (st->sock == -1) {
        emd_log(LOG_DEBUG, "Error to open socket: %s", strerror(errno));
        return -1;
	}

	memset(&st->local, 0, sizeof(st->local));
	st->local.sin_family = AF_INET;
	st->local.sin_addr.s_addr = htonl(INADDR_ANY);
	st->local.sin_port = htons(0);

	memset(&st->remote, 0, sizeof(st->remote));
	st->remote.sin_family = AF_INET;
	st->remote.sin_addr.s_addr = inet_addr(remote);
	st->remote.sin_port = htons(port);

	if (bind(st->sock, (struct sockaddr *)&st->local, sizeof(st->local)) == -1) {
        emd_log(LOG_DEBUG, "Error to bind socket: %s", strerror(errno));
        return -1;
	}

	struct timespec tv = (struct timespec){1, 0};

	if (setsockopt(st->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
        emd_log(LOG_DEBUG, "setsockopt(SO_SNDTIMEO) failed: %s", strerror(errno));
        return -1;
	}

	if(setsockopt(st->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1)
	{
        emd_log(LOG_DEBUG, "setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
        return -1;
	}
    
	st->pcount = 0;

	return 0;
}

int sock_tlv_send_recv(struct sock_tlv *st, uint8_t out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len)
{
	int ret = -1;
	char *out_buf;
	int out_buf_len = out_len, sz;
	uint8_t tag;
	uint16_t ccount;
	socklen_t sl = 0;

	out_buf = malloc(out_len);
	memcpy(out_buf, out_value, out_len);

	if (encode(st->pcount++, out_tag, (uint8_t **)&out_buf, &out_buf_len) == -1) {
		emd_log(LOG_DEBUG, "encode() failed: %s", strerror(errno));
		goto exit;
	}

    for (int retry = 0; retry < SEND_RECV_RETRY; retry++) {
		int errn;

        if (sendto(st->sock, out_buf, out_buf_len, 0, (const struct sockaddr *)&st->remote, sizeof(struct sockaddr_in)) == -1) {
            errn = errno;
            emd_log(LOG_DEBUG, "sendto failed:(%d) %s", errno, strerror(errno));
            if (errn == 11)
                continue;
            goto exit;
        }

		while (1) {
			if ((sz = recvfrom(st->sock, st->recv_buf, RECV_BUF_SIZE, 0, NULL, &sl)) == -1) {
				errn = errno;
				emd_log(LOG_WARNING,"recvfrom failed:(%d) %s", errno, strerror(errno));
				if (errn == 11)
					break;
				goto exit;
			}

			if (decode(&ccount, &tag, (uint8_t **)&st->recv_buf, &sz) == -1) {
				emd_log(LOG_DEBUG, "decode failed: %s", strerror(errno));
				goto exit;
			}
			if (tag != out_tag && tag != 0x31 && tag != 0x32)
					continue;
			ret = in_len < sz? in_len: sz;

			if (in_tag)
				*in_tag = tag;
			if (in_value)
				memcpy(in_value, st->recv_buf, ret);
			goto exit;
		}
	}

exit:
	if (out_buf)
		free(out_buf);

	return ret;
}

int sock_tlv_close(struct sock_tlv *st)
{
	if (st->recv_buf) {
            free(st->recv_buf);
            st->recv_buf = NULL;
	}

	if (st->sock != -1) {
            close(st->sock);
            st->sock = -1;
	}

	return 0; 
}
