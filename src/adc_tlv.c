#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/if_alg.h>
#include <sys/param.h>

#define _BSD_SOURCE
#include <endian.h>


static int crc32(uint8_t *data, size_t len, uint32_t *crc32);
static void make_tlv(uint8_t tag, uint8_t **data, int *len);
static int break_tlv(uint8_t *tag, uint8_t **data, int *len);
static void stuff(uint8_t **data, int *len);
static void unstuff(uint8_t **data, int *len);

/* Needed in kernel
 * CONFIG_CRYPTO_USER_API=y
 * CONFIG_CRYPTO_USER_API_HASH=y
 */
int crc32(uint8_t *data, size_t len, uint32_t *crc32)
{
	int ret = -1;
	int sds[2] = { -1, -1 };

	struct sockaddr_alg sa = {
		.salg_family = AF_ALG,
		.salg_type   = "hash",
		.salg_name   = "crc32"
	};

	if ((sds[0] = socket(AF_ALG, SOCK_SEQPACKET, 0)) == -1 )
		goto err;

	if( bind(sds[0], (struct sockaddr *) &sa, sizeof(sa)) != 0 )
		goto err;

	if( (sds[1] = accept(sds[0], NULL, 0)) == -1 )
		goto err;

	if (send(sds[1], data, len, MSG_MORE) != len)
		goto err;

	if(read(sds[1], crc32, sizeof(uint32_t)) != sizeof(uint32_t))
		goto err;
	ret = 0;

err:
	if (sds[0] != -1)
		close(sds[0]);
	if (sds[1] != -1)
		close(sds[1]);

	return ret;
}

void make_tlv(uint8_t tag, uint8_t **data, int *len)
{
	uint8_t *buf;
	int sz;
	if (*len < 0x7f) {
		buf = (uint8_t *)malloc(1 + 1 + *len);
		buf[0] = tag;
		buf[1] = *len;
		sz = 1;
	} else {
		uint8_t i = 0;
		sz = *len;
		for (; sz; i++)
			sz /= 256;
		sz = i;

		buf = (uint8_t *)malloc(1 + 1 + sz + *len);
		buf[0] = tag;
		buf[1] = 0x80 + i;
		for (i = 0; i < sz; i++)
			buf[2 + i] = ((*len) >> (sz - 1 - i)*8) & 0xff;
		sz++;
	}

	memcpy(&buf[1 + sz], *data, *len);
		
	free(*data);
	*data = buf;
	*len = sz + 1 + *len;
}

// @return -1 parse failed
int break_tlv(uint8_t *tag, uint8_t **data, int *len)
{
	if (*len < 2) return -1;

	uint8_t *buf = *data;

	*tag = *buf++;
	(*len)--;

	int sz_len, sz_val;
	if (*buf & 0x80) {
		sz_len = (*buf & 0x7f) + 1;
		sz_val = *(buf + 1);
		for (int i = 2; i < sz_len; i++) {
			sz_val <<= 8;
			sz_val += *(buf + i);
		}
	} else {
		sz_len = 1;
		sz_val = *buf;
	}
	buf += sz_len;
	(*len) -= sz_len;
	if ((*len) - sz_val < 0)
		return -1;
	
	memmove(*data, buf, *len);

	return 0;
}

void stuff(uint8_t **data, int *len)
{
	int buf_len = *len + 2, i = 0, j = 0;
	uint8_t *buf = (uint8_t *)malloc(buf_len);
	
	buf[j++] = 0xC0;
	for (; i < *len; i++, j++) {
		if ((*data)[i] == 0xC0) {
			buf_len++;
			buf = realloc(buf, buf_len);
			buf[j++] = 0xDB;
			buf[j] = 0xDC;
		} else if ((*data)[i] == 0xDB) {
			buf_len++;
			buf = realloc(buf, buf_len);
			buf[j++] = 0xDB;
			buf[j] = 0xDD;
		} else
			buf[j] = (*data)[i];
	}
	buf[j] = 0xC0;

	free(*data);

	*data = buf;
	*len = buf_len;
}

void unstuff(uint8_t **data, int *len)
{
	int i = 0, j = 0;
	uint8_t *buf = *data;
	int db = 0;
	for (; i < *len; i++) {
		if (buf[i] == 0xC0) {
			db = 0;
			continue;
		} else if (buf[i] == 0xDB) {
			db = 1;
			continue;
		} else if (buf[i] == 0xDC && db) {
			db = 0;
			buf[j++] = 0xC0;
		} else if (buf[i] == 0xDD && db) {
			db = 0;
			buf[j++] = 0xDB;
		} else {
			if (db) {
				buf[j++] = 0xDB;
				db = 0;
			}
			buf[j++] = buf[i];
		}
	}
	*len = j;
}

// stuff(make_tag(tag 0x80, pcount + data + crc32(pcount + data))) 
int encode(uint16_t pcount, uint8_t tag, uint8_t **data, int *len)
{
	make_tlv(tag, data, len);

	int sz = sizeof(pcount) + *len + sizeof(crc32);
	uint8_t *buf = (uint8_t *)malloc(sz);
	uint8_t *ptr = buf;
	*((uint16_t *)ptr) = htobe16(pcount);
	ptr += sizeof(pcount);
	memcpy(ptr, *data, *len);
	ptr += *len;
	
	uint32_t crc;
	if (crc32(buf, *len + sizeof(uint16_t), &crc) == -1) {
		int en = errno;	
		free(buf);
		errno = en;
		return -1;
	}
	*((uint32_t *)ptr) = htobe32(crc);
	make_tlv(0x80, &buf, &sz);
	stuff(&buf, &sz);

	free(*data);
	*data = buf;
	*len = sz;

	return 0;
}

int decode(uint16_t *pcount, uint8_t *tag, uint8_t **data, int *len)
{
	unstuff(data, len);
	if (break_tlv(tag, data, len) == -1)
		return -1;
	if (*tag != 0x81)
		return -1;
	uint32_t crc;
	if(crc32(*data, *len - sizeof(uint32_t), &crc) == -1)		return -1;
	if (htobe32(crc) != *(uint32_t *)(*data + *len - sizeof(uint32_t)))
		return -1;
	*pcount = be16toh(*(uint16_t *)(*data));
	(*len) -= (sizeof(uint16_t) + sizeof(uint32_t));
	memmove(*data, *data + sizeof(uint16_t), *len);
	if (break_tlv(tag, data, len) == -1)
		return -1;
	return 0;
}

#if 0
int main(int argc, char *argv[])
{
	uint32_t len;
	uint8_t *buf = malloc(64);
	
	memcpy(buf, "123456", 6);
	len = 5;
	make_tlv(1, &buf, &len);
	printf("%02x %02x %02x %02x %02x %02x len = %u\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], len);
	free(buf);

	buf = malloc(640);
	memcpy(buf, "123456", 6);
	len = 201;
	make_tlv(2, &buf, &len);
	printf("%02x %02x %02x %02x %02x %02x len = %u\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], len);
	free(buf);

	buf = malloc(640);
	memcpy(buf, "123456", 6);
	len = 512;
	make_tlv(3, &buf, &len);
	printf("%02x %02x %02x %02x %02x %02x len = %u\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], len);
	free(buf);
}
#endif
