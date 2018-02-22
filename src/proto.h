#ifndef PROTO_H_
#define PROTO_H_

#include <stdint.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/ether.h>

#include "sv_read.h"

/* Протокол обмена.
 * 1. Формат пакета:
 *		2 байта (unsigned short) - длина пакета
 *		1 байт - код сообщения
 *		<длина_пакета> - 3 байт - данные
 *
 *		Коды 0-127, запрос и ответ, если ошибка,
 *		то первый бит кода ответа выставить в 1
 */

#define SV_ID_MAX_LEN 64
#define VERSION_MAX_LEN 24

enum REQ_CODES {
	GET_PROTO_VERSION = 1,
	STATE_REQ,
	SET_TIME_REQ,
	GET_ADC_PROP_REQ,
	SET_ADC_PROP_REQ,
	SET_ADC_PARAM_REQ,
	GET_STREAMS_PROP_REQ,
	SET_STREAMS_PROP_REQ,
	GET_STREAMS_LIST_REQ,
	GET_SYNC_PROP_REQ,
	SET_SYNC_PROP_REQ,
	GET_VERSION_REQ,
	SET_NETWORK_REQ,
	GET_CALC_COMPARATOR_REQ,
	GET_CALC_DATA_REQ,
	GET_CALC_HARMONICS_REQ,
	GET_CALC_UI_REQ,
	GET_CALC_P_REQ,
	GET_CALC_A_REQ,
	GET_CALIB_COEF_REQ,
	SET_CALIB_COEF_REQ,
	GET_CALIB_NULL_REQ,
	GET_CALIB_SCALE_REQ,
	GET_CALIB_ANGLE_REQ,
};

enum ERR_CODES {
	ERR_RETRY = 2,
	ERR_NOT_AVAILABLE
};

typedef struct __attribute__((__packed__)) pdu {
	uint16_t len;
	int8_t msg_code;
	int8_t data[];
} pdu_t;

struct __attribute__((__packed__)) err_resp {
	uint8_t err_code;
};

struct __attribute__((__packed__)) set_time_req {
	int32_t time;
};

#define STREAM1_OK 0x01
#define STREAM2_OK 0x10

struct __attribute__((__packed__)) state_resp {
	uint8_t streams_state;
};

struct __attribute__((__packed__)) mac_resp {
	char mac[17];
};

struct __attribute__((__packed__)) adc_properties {
	union __attribute__((__packed__)) {
		struct __attribute__((__packed__)) {
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
	struct ether_addr src_mac;
	struct ether_addr dst_mac;
	uint8_t rate;	// enum SV_DISCRETE 
	char sv_id[SV_ID_MAX_LEN];
};

typedef struct adc_properties adc_prop_resp;

enum ADC_PARAM_TYPE {
	ADC_PARAM_TYPE_RANGE,
	ADC_PARAM_TYPE_SRC_MAC,
	ADC_PARAM_TYPE_DST_MAC,
	ADC_PARAM_TYPE_RATE,
	ADC_PARAM_TYPE_SV_ID,
	ADC_PARAM_TYPE_CALIB_NULL,
	ADC_PARAM_TYPE_CALIB_SCALE,
	ADC_PARAM_TYPE_CALIB_SHIFT,
};

typedef struct __attribute__((__packed__)) adc_param_req {
	uint8_t type;
	union __attribute__((__packed__)) {
		struct __attribute__((__packed__)) {
			uint8_t stream_mask;
			uint8_t range;
			union __attribute__((__packed__)) {
				int null[PHASES_IN_STREAM];
				float scale[PHASES_IN_STREAM];
				float shift[PHASES_IN_STREAM];
			};
		};
		struct ether_addr mac;
		uint8_t rate;
		char sv_id[SV_ID_MAX_LEN];
	};
} adc_param_req;

typedef struct __attribute__((__packed__)) stream_property {
	struct ether_addr src_mac;
	struct ether_addr dst_mac;
	char sv_id[SV_ID_MAX_LEN];
} stream_property;

struct __attribute__((__packed__)) streams_properties {
	stream_property data[2];
};

typedef struct streams_properties streams_prop_resp;

typedef struct __attribute__((__packed__)) {
	int count;
	stream_property data[];
} streams_list;

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

typedef struct __attribute__((__packed__)) calc_req {
	struct timeval time_stamp;
	uint8_t stream[2];
} calc_req;

typedef struct __attribute__((__packed__)) versions_resp {
	char emd[VERSION_MAX_LEN];
	char adc[VERSION_MAX_LEN];
	char sync[VERSION_MAX_LEN];
} versions_resp;

typedef struct __attribute__((__packed__)) network {
	char addr[INET_ADDRSTRLEN];
	char mask[INET_ADDRSTRLEN];
	char gateway[INET_ADDRSTRLEN];
} network;

/********** calc ***************/
typedef struct  __attribute__((__packed__)) calc_resp {
	calc_req resp;
	uint8_t data[];
} calc_resp; 

typedef struct __attribute__((__packed__)) calc_comparator {
	double rms;
	double dc;
	double f_1h;
	double rms_1h;
	double phi;
	double thd;
} calc_comparator;


typedef struct __attribute__((__packed__)) calc_harmonic {
	double ampl;
} calc_harmonic;

typedef struct __attribute__((__packed__)) calc_harmonics {
	uint8_t harmonics_num;
	double f_1h;
	struct __attribute__((__packed__)) calc_harmonic h[];
} calc_harmonics;

typedef struct __attribute__((__packed__)) calc_data_req {
	calc_req req;
	uint32_t scale;
	uint32_t begin;
	uint32_t length;
	uint32_t counts_limit;
} calc_data_req;

typedef struct __attribute__((__packed__)) calc_data {
	uint32_t counts;
	float data[];
} calc_data;

typedef struct __attribute__((__packed__)) calc_multimeter_req {
	calc_req req;
	uint8_t reference[2];
} calc_multimeter_req;

typedef struct __attribute__((__packed__)) calc_ui {
	double rms;
	double rms_1h;
	double mid;
	double thd;
} calc_ui;

struct __attribute__((__packed__)) dvalue {
	double value;
};

typedef struct dvalue calc_ui_diff;

typedef struct __attribute__((__packed__)) calc_p {
	double p;
	double q;
	double s;
	double rms_u;
	double rms_i;
	double p_1h;
	double q_1h;
	double s_1h;
	double cos_phi;
	double sin_phi;
} calc_p;

typedef struct dvalue calc_a;

typedef struct __attribute__((__packed__)) calib_angle {
	double phi;
	double freq_1h;
} calib_angle;
		 
#endif
