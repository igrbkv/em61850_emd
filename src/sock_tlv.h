#ifndef SOCK_TLV_H_
#define SOCK_TLV_H_


#include <arpa/inet.h>

struct sock_tlv {
	int sock;
	uint16_t port;
	struct sockaddr_in local;
	struct sockaddr_in remote;
	uint16_t scount;
	uint16_t rcount;
	uint8_t tag;
	int len;
	uint8_t *value;
	char *recv_buf;
	int size;	// recv_buf && value size
	int timeout;
	int cur;
	int (*init)(struct sock_tlv *st, const char *remote, uint16_t port);
	int (*send_recv)(struct sock_tlv *st);
	int (*poll)(struct sock_tlv *st);
	int (*poll_cb)(uint16_t ccount, uint8_t tag, int len, const uint8_t *buf);
	int (*close)(struct sock_tlv *st);
};

int sock_tlv_init(struct sock_tlv *st, const char *remote, uint16_t port);
int sock_tlv_send_recv(struct sock_tlv *st);
int sock_tlv_poll(struct sock_tlv *st);
int sock_tlv_close(struct sock_tlv *st);
#endif
