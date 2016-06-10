#ifndef SOCK_TLV_H_
#define SOCK_TLV_H_


#include <arpa/inet.h>

struct sock_tlv {
	int sock;
	uint16_t port;
	struct sockaddr_in local;
	struct sockaddr_in remote;
	uint16_t pcount;
	char *recv_buf;
	int (*init)(struct sock_tlv *st, const char *remote, uint16_t port);
	int (*send_recv)(struct sock_tlv *st, uint8_t out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len);
	int (*close)();
};

int sock_tlv_init(struct sock_tlv *st, const char *remote, uint16_t port);
int sock_tlv_send_recv(struct sock_tlv *st, uint8_t out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len);
int sock_tlv_close(struct sock_tlv *st);
#endif
