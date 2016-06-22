#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/param.h>

#define _BSD_SOURCE
#include <endian.h>

#include "tlv.h"
#include "log.h"


enum PARSE_STATE {
	PS_INITIAL,
	PS_C0,
	PS_TAG,
	PS_LEN,
	PS_VALUE
};

static int stuff(uint8_t *data, int *len, int buf_size);

static char *print_buf(uint8_t *data, int data_len)
{
	int buf_len = data_len*2 + 1;
	char *buf = malloc(buf_len + 1);
	int n = 0;
	for (int i = 0; i < data_len; i++) {
		n += snprintf(&buf[n], buf_len - n, "%02X", data[i]);
	}
	buf[n] = '\0';
	return buf;
}

int make_tlv(uint8_t tag, uint8_t *data, int *len, int buf_size)
{
	int i;
	int data_len = *len;
	int tlv_len = (data_len + 0x7f)/256 + 1;
	if ((1 + tlv_len + data_len) > buf_size)
		goto buf_size_err;

	memmove(&data[1 + tlv_len], data, data_len);
	data[0] = tag;
	if (tlv_len == 1)
		data[1] = data_len;
	else {
		data[1] = 0x80 + tlv_len - 1;
		for (i = 0; i < (tlv_len - 1); i++)
			data[2 + i] = (data_len >> (tlv_len - i)*8) & 0xff;
	}
	data_len += 1 + tlv_len; 
	*len = data_len;
	return 0;

buf_size_err:
	emd_log(LOG_DEBUG, "%s the buffer size is too small", __func__);
	return -1;
}

int stuff(uint8_t *data, int *len, int buf_size)
{
	int i = 0;
	int data_len = *len;
	
	memmove(&data[1], data, data_len);
	data_len++;
	if (data_len > buf_size)
		goto buf_size_err;

	data[i++] = 0xC0;
	for (; i < data_len; i++) {
		if (data[i] == 0xC0 || data[i] == 0xDB) {
			if (data_len + 1 > buf_size)
				goto buf_size_err;
			memmove(&data[i+1], &data[i], data_len-i);
			data_len++;
			data[i+1] = data[i] == 0xC0? 0xDC: 0xDD;
			data[i++] = 0xDB;
		} 
	}
	if (++data_len > buf_size)
		goto buf_size_err;
	data[i] = 0xC0;

	*len = data_len;
	return 0;
buf_size_err:
	emd_log(LOG_DEBUG, "%s the buffer size is too small", __func__);
	return -1;
}

int encode(uint16_t pcount, uint8_t tag, uint8_t *data, int *len, int buf_size)
{
	if (make_tlv(tag, data, len, buf_size) == -1)
		return -1;

	if (*len + sizeof(uint16_t) + sizeof(uint32_t) > buf_size)
		goto buf_size_err;

	memmove(&data[sizeof(uint16_t)], data, *len);
	*((uint16_t *)data) = htobe16(pcount);
	*len += sizeof(uint16_t);
	uint32_t crc = crc32(data, *len);

	*((uint32_t *)&data[*len]) = htobe32(crc);
	*len += sizeof(uint32_t);

	if (make_tlv(0x80, data, len, buf_size) == -1)
		goto buf_size_err;

	if (emd_debug > 1) {
		char *str = print_buf(data, *len);
		emd_log(LOG_DEBUG, "out tlv:%s", str);
		free(str);
	}

	if (stuff(data, len, buf_size) == -1)
		goto buf_size_err;

	return 0;

buf_size_err:
	emd_log(LOG_DEBUG, "%s the buffer size is too small", __func__);
	return -1;
}

// data => msg tlv => tlv 
// @return - number of bytes reads
// if *len == -1 msg not found
int decode(uint16_t *pcount, uint8_t *tag, uint8_t *data, int *len)
{
	if (*len <= 0 || data == NULL)
		return 0;

	if (emd_debug > 1) {
		char *str = print_buf(data, *len);
		emd_log(LOG_DEBUG, "in stuff:%s", str);
		free(str);
	}

	int bytes_reads = 0;
	int i = 0, j = 0;
	uint8_t *buf = data;
	int db = 0;
	int buf_len = *len,
		part_len,
		value_len;
	uint8_t *value_ptr;
	int parsed = 0;

	*len = -1;	// msg not found

	enum PARSE_STATE ps = PS_INITIAL;
	// msg parse
	for (; !parsed && i < buf_len; i++) {
		if (buf[i] == 0xC0) {
			db = 0;
			ps = PS_C0;
			bytes_reads = i;
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

		uint8_t ch = buf[j-1];
		switch (ps) {
			case PS_INITIAL:
				break;
			case PS_C0:
				if (ch == 0x81) // || ch == 0x80)
					ps = PS_TAG;
				break;
			case PS_TAG:
				if (ch & 0x80) {
					part_len = (ch & 0x7f);
					if (part_len > 4) {
						ps = PS_INITIAL;
						break;
					}
					value_len = 0;
				} else {
					part_len = 0;
					value_len = ch;
				}
				ps = PS_LEN;
				break;
			case PS_LEN:
				if (part_len == 0) {
					if (value_len < sizeof(uint16_t) + sizeof(uint32_t))
						ps = PS_INITIAL;
					ps = PS_VALUE;
					part_len = value_len;
					value_ptr = &buf[j-1];
				} else {
					value_len = (value_len << 8) + ch;
					part_len--;
					break;
				}
			case PS_VALUE:
				if (--part_len == 0) {
					uint32_t crc;
					crc = crc32(value_ptr, value_len - sizeof(uint32_t));
					if (htobe32(crc) != *(uint32_t *)&value_ptr[value_len - sizeof(uint32_t)]) {
						emd_log(LOG_DEBUG, "crc error!");
						ps = PS_INITIAL;
					} else {
						if (pcount)
							*pcount = be16toh(*(uint16_t *)value_ptr);
						buf_len = value_len - sizeof(uint16_t) - sizeof(uint32_t);
						memmove(data, value_ptr + sizeof(uint16_t), buf_len);
						parsed = 1;
						bytes_reads = i + 1;
					}
				}
				break;
		}
	}

	// parse unpacked tlv
	if (parsed) {
		parsed = 0;
		ps = PS_INITIAL;
		for (j = 0; !parsed && j < buf_len; j++) {
			uint8_t ch = buf[j];
			switch (ps) {
				case PS_INITIAL:
					if (tag)
						*tag = ch;
					ps = PS_TAG;
					break;
				case PS_TAG:
					if (ch & 0x80) {
						part_len = (ch & 0x7f);
						if (part_len > 4) {
							ps = PS_INITIAL;
							break;
						}
						value_len = 0;
						ps = PS_LEN;
					} else {
						value_len = ch;
						value_ptr = &buf[j+1];
						if (value_len == 0)
							parsed = 1;
						else {
							part_len = value_len;
							ps = PS_VALUE;
						}
					}
					break;
				case PS_LEN:
					value_len = (value_len << 8) + ch;
					if (--part_len == 0) {
						ps = PS_VALUE;
						value_ptr = &buf[j+1];
						part_len = value_len;
						if (value_len == 0)
							parsed = 1;
					}
					break;
				case PS_VALUE:
					if (--part_len == 0)
						parsed = 1;
					break;
			}
		}
		if (parsed) {
			*len = value_len;
			memmove(data, value_ptr, value_len);

			if (emd_debug > 1) {
				char *str = print_buf(data, *len);
				emd_log(LOG_DEBUG, "in tlv:%s", str);
				free(str);
			}
		} else if (emd_debug > 1)
			emd_log(LOG_DEBUG, "tlv parser failed");
	} else if (emd_debug > 1)
		emd_log(LOG_DEBUG, "msg was not parsed");
		
	return bytes_reads;
}



uint32_t crc32(uint8_t * buf, size_t len) 
{
	static const uint32_t crc32_table[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
	};

	unsigned int crc = 0xFFFFFFFF;
	while (len--)
		crc = (crc >> 8) ^ crc32_table[(crc ^ *buf++) & 0xFF];
	return crc ^ 0xFFFFFFFF;
}

#ifdef TEST
int main(int argc, char *argv[])
{
	emd_debug = 2;
	uint32_t len = 512, _len;
	uint16_t pcount = 1, _pcount;
	uint8_t tag = 0xb1, _tag;
	uint8_t *buf = malloc(len);
	for (int i = 0; i < len; i++)
		buf[i] = i & 0xff;
	uint8_t *_buf = malloc(len);
	memcpy(_buf, buf, len);
	_len = len;
	
	encode(pcount, tag, &_buf, &_len);
	char *str = print_buf(_buf, _len);
	printf("encode: pcount=%u tag=%u data=%s\n", pcount, tag, str);

	decode(&_pcount, &_tag, _buf, &_len);
	char *str1 = print_buf(_buf, _len);
	printf("decode: pcount=%u tag=%u data=%s\n", _pcount, _tag, str1);
	free(buf);
}
#endif
