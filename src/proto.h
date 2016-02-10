#ifndef PROTO_H_
#define PROTO_H_

#include <stdint.h>
#include <sys/time.h>

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

#define SV_ID_MAX_LEN 64

enum REQ_CODES {
	STATE_REQ = 1,
	SET_TIME_REQ,
	GET_MAC_REQ,
	GET_ADC_PROP_REQ,
	SET_ADC_PROP_REQ,
	GET_STREAMS_PROP_REQ,
	SET_STREAMS_PROP_REQ,
	GET_U_AB_REQ,
	GET_UA_UA_REQ,
};

enum ERR_CODES {
	NOT_AVAILABLE
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

struct req {
};

struct __attribute__((__packed__)) set_time_req {
	int32_t time;
};

#define STREAM1_MASK 0x0f
#define STREAM1_OK 0x01
#define STREAM2_MASK 0xf0
#define STREAM2_OK 0x10

struct __attribute__((__packed__)) state_resp {
	uint8_t streams_state;
};

struct __attribute__((__packed__)) mac_resp {
	char mac[17];
};

struct __attribute__((__packed__)) adc_properties {
	union {
		struct {
			uint8_t ua_range;
			uint8_t ub_range;
			uint8_t uc_range;
			uint8_t un_range;
			uint8_t ia_range;
			uint8_t ib_range;
			uint8_t ic_range;
			uint8_t in_range;
		};
		uint8_t range[8]; // enum U_RANGE or I_RANGE
	};
	char src_mac[17];
	char dst_mac[17];
	uint8_t rate;	// enum SV_DISCRETE 
	char sv_id[SV_ID_MAX_LEN];
};


typedef struct adc_properties adc_prop_resp;

struct __attribute__((__packed__)) streams_properties {
	uint8_t stream1;
	char mac1[17];
	char sv_id1[SV_ID_MAX_LEN];
	uint32_t u_trans_coef1; 
	uint32_t i_trans_coef1; 
	uint8_t stream2;
	char mac2[17];
	char sv_id2[SV_ID_MAX_LEN];
	uint32_t u_trans_coef2; 
	uint32_t i_trans_coef2; 
};

typedef struct streams_properties streams_prop_resp;

struct __attribute__((__packed__)) u_ab {
	struct timeval ts;
	double rms_ua;
	double rms_ub;
	float values[];
}; 

typedef struct u_ab u_ab_resp;

struct __attribute__((__packed__)) ua_ua {
	struct timeval ts;
	uint8_t flags;	// STREAM1_OK & STREAM2_OK
	double rms_ua1;
	double rms_ua2;
	float values[];
}; 

typedef struct ua_ua ua_ua_resp;
#endif
