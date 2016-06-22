#ifndef TLV_H_
#define TLV_H_

int encode(uint16_t pcount, uint8_t tag, uint8_t *data, int *len, int buf_size);
int decode(uint16_t *pcount, uint8_t *tag, uint8_t *data, int *len);

int make_tlv(uint8_t tag, uint8_t *data, int *len, int buf_size);

struct sockaddr_in;
int tlv_send_recv(int sock, struct sockaddr_in *remote, int16_t pcount, char out_tag, char *out_value, int out_len, char *in_tag, char *in_value, int in_len);
uint32_t crc32(uint8_t *buf, size_t len);
#endif
