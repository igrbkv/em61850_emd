#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

#include "log.h"

/* Протокол обмена.
 * 1. Формат пакета:
 *		1 байт - код сообщения
 *		2 байта (unsigned short) - длина данных
 *		<длина_данных> байт - данные
 * 
 *		Коды 0-127, запрос и ответ, если ошибка,
 *		то первый бит кода ответа выставить в 1, а
 *		в длину данных записать код ошибки.
 */

#define MAX_DIFF_TIME 2

enum REQ_CODES {
	STATE_REQ = 1,
};

enum ERR_CODES {
	ERR_SIZE,
};

typedef struct __attribute__((__packed__)) pdu {
	int8_t msg_code;
	uint16_t data_len;
	int8_t data[];
} pdu_t;

struct __attribute__((__packed__)) err_resp {
	int8_t msg_code;
	uint8_t err_code;
};

struct state_req {
	pdu_t hdr;
	int32_t time;
};

struct state_resp {
	pdu_t hdr;
};


static void make_err_resp(int8_t code, uint8_t err, void **msg, int *len);
static void apply_time(int32_t client_time);


void make_err_resp(int8_t code, uint8_t err, void **msg, int *len)
{
	struct err_resp *buf = malloc(sizeof(struct err_resp));
	buf->msg_code = code | 0x80;
	buf->err_code = err;
	*msg = buf;
	*len = sizeof(struct err_resp);
}

int parse_request(void *in, int in_len, void **out, int *out_len)
{
	int ret = -1;
	if (in_len < sizeof(pdu_t)) {
		emd_log(LOG_DEBUG, "request size is too small!");
		return -1;
	}
	pdu_t *hdr = (pdu_t *)in;

	switch (hdr->msg_code) {
		case STATE_REQ:
			if (ntohs(hdr->data_len) != sizeof(int32_t)) {
				emd_log(LOG_DEBUG, "STATE_REQ error data size!");
				break;
			}				
			struct state_req *req = (struct state_req *)in;
			ret = sizeof(struct state_req);
			apply_time(ntohl(req->time));
			struct state_resp *resp = malloc(sizeof(struct state_resp));
			resp->hdr.msg_code = STATE_REQ;
			resp->hdr.data_len = htons(0);
			*out = (void *)resp;
			*out_len = sizeof(struct state_resp);
			break;
	}

	return ret;
}

void apply_time(int32_t client_time)
{
	time_t server_time = time(NULL);
	time_t diff = server_time > (time_t)client_time? 
		server_time - (time_t)client_time:
		(time_t)client_time - server_time;
	if (diff < MAX_DIFF_TIME) 
		return;
	
	struct timeval tv = {client_time, 0};
	// FIXME reset the sv reading
#if 0
	settimeofday(&tv, NULL);
#endif
}
