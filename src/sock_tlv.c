#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_alg.h>
#include <arpa/inet.h>
#include <poll.h>

#include "log.h"
#include "tlv.h"
#include "sock_tlv.h"

#define DEFAULT_BUF_SIZE 1512
#define SEND_RECV_RETRY 3 

static int sock_tlv_rcv(struct sock_tlv *st, int timeout);

int sock_tlv_init(struct sock_tlv *st, const char *remote, uint16_t port)
{
	if (!st->size)
		st->size = DEFAULT_BUF_SIZE;

	st->recv_buf = malloc(st->size);
	st->value = malloc(st->size);

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


	st->scount = 0;

	return 0;
}

int sock_tlv_rcv(struct sock_tlv *st, int timeout)
{
	int sz = 0;
	socklen_t sl = 0;
	struct pollfd pfd[1];
	pfd[0].fd = st->sock;
	pfd[0].events = POLLIN;

	int ret = poll(pfd, 1, timeout);

	// error
	if (ret == -1) {
		emd_log(LOG_DEBUG, "%s poll() failed: (%d) %s", __func__, errno, strerror(errno));
		return -1;
	}
	// timeout
	if (ret == 0) { 
		if (timeout)
			emd_log(LOG_DEBUG, "%s poll() timeout", __func__);
		return 0;
	}
	if (pfd[0].revents & POLLIN) {
		if ((sz = recvfrom(st->sock, &st->recv_buf[st->cur], 
			DEFAULT_BUF_SIZE - st->cur, 0, NULL, &sl)) == -1) {
			emd_log(LOG_WARNING, "%s recvfrom failed:(%d) %s", __func__, errno, strerror(errno));
			return -1;
		}
		st->cur += sz;
	}
	return sz;
}

int sock_tlv_poll(struct sock_tlv *st)
{
	int ret;
	while ((ret = sock_tlv_rcv(st, 0))) {
		// error
		if (ret == -1)
			return -1;

		if (st->poll_cb) {
			st->cur -= decode(&st->rcount, &st->tag, st->value, &st->len);
			if (st->len == -1)
				continue;
			st->poll_cb(st->rcount, st->tag, st->len, st->value);
		} else
			// erase
			st->cur = 0;
	}
	return 0;
}

int sock_tlv_send_recv(struct sock_tlv *st)
{
	int ret = -1;
	uint8_t tag = st->tag;

	if (encode(st->scount++, st->tag, st->value, &st->len, st->size) == -1) {
		emd_log(LOG_DEBUG, "encode() failed: %s", strerror(errno));
		goto exit;
	}

    for (int retry = 0; retry < SEND_RECV_RETRY; retry++) {
		// erase socket buffer
		if (st->poll)
			st->poll(st);

        if (sendto(st->sock, st->value, st->len, 0, (const struct sockaddr *)&st->remote, sizeof(struct sockaddr_in)) == -1) {
            emd_log(LOG_DEBUG, "sendto() failed:(%d) %s", errno, strerror(errno));
            goto exit;
        }

		int timeout = st->timeout;
		while ((ret = sock_tlv_rcv(st, timeout))) {
			if (ret == -1)
				return -1;
			// отладочные сообщения каждые 100 мс
			timeout = timeout - 100 < 0? 0: timeout - 100;
			st->len  = st->cur;
			memcpy(st->value, st->recv_buf, st->len);
			st->cur -= decode(&st->rcount, &st->tag, st->value, &st->len);
			if (st->len == -1)
				continue;

			if (tag != st->tag && st->tag != 0x31 && st->tag != 0x32)
				continue;
			ret = st->len;

			goto exit;
		}
	}

exit:

	return ret;
}

int sock_tlv_close(struct sock_tlv *st)
{
	if (st->recv_buf) {
		free(st->recv_buf);
		st->recv_buf = NULL;
	}
	if (st->value) {
		free(st->value);
		st->value = NULL;
	}

	if (st->sock != -1) {
		close(st->sock);
		st->sock = -1;
	}

	return 0; 
}
