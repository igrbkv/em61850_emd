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
	GET_ADC_PROP_REQ,
	SET_ADC_PROP_REQ,
	GET_STREAMS_PROP_REQ,
	SET_STREAMS_PROP_REQ,
	GET_SYNC_PROP_REQ,
	SET_SYNC_PROP_REQ,
	GET_CALC_REQ,
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
	char src_mac1[17];
	char dst_mac1[17];
	char sv_id1[SV_ID_MAX_LEN];
	uint32_t u_trans_coef1; 
	uint32_t i_trans_coef1; 
	uint8_t stream2;
	char src_mac2[17];
	char dst_mac2[17];
	char sv_id2[SV_ID_MAX_LEN];
	uint32_t u_trans_coef2; 
	uint32_t i_trans_coef2; 
};

typedef struct streams_properties streams_prop_resp;

struct __attribute__((__packed__)) output_properties {
	uint8_t mode;
	double freq;
	double impulse_duration;
	double impulse_delay;
};

struct __attribute__((__packed__)) sync_properties {
	uint8_t in_sig;
	struct output_properties out[2];
};

typedef struct sync_properties sync_prop_resp;

#define STREAM2_START_IDX 8
#define I_START_IDX 0
#define PHASES_NUM 4

// 0..7  (Ia..Un) for stream 1
// 8..15 (Ia..Un) for stream 2
struct __attribute__((__packed__)) calc_req {
	uint8_t idx1;
	uint8_t idx2;
};

struct __attribute__((__packed__)) harmonic {
	double f;
	double k;
	double ampl;
};

struct __attribute__((__packed__)) calc_results {
	double rms;
	double dc;
	double f_1h;
	double rms_1h;
	double phi;
	double thd;
	uint8_t harmonics_num;
	struct harmonic h[];
};

typedef struct  __attribute__((__packed__)) calc {
	uint64_t ts_sec;	// timeval64
	uint64_t ts_usec;	// timeval64
	uint8_t valid1;
	uint8_t valid2;
	uint8_t data[];
} calc_resp; 
#endif
