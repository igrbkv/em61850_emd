#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <endian.h>
#include <stdio.h>
#include <errno.h>

#include "log.h"
#include "tcp_server.h"
#include "sv_read.h"
#include "settings.h"
#include "adc_client.h"
#include "sync_client.h"
#include "proto.h"
#include "calc.h"
#include "calc_data.h"

#define MAX_DIFF_TIME 2

int correct_time = 1; // Время усанавливается по планшету один раз за сессию
static void make_err_resp(int8_t code, uint8_t err, void **msg, int *len);
static void apply_time(int32_t client_time);
static void calc_results_to_be64(struct calc_general *cr);
static void calc_power_to_be64(struct calc_power *cp);
static void set_network(const network *net);
static int phases(calc_req *req)

void make_err_resp(int8_t code, uint8_t err, void **msg, int *len)
{
	int sz = sizeof(pdu_t) + sizeof(struct err_resp);
	pdu_t *resp = malloc(sz);
	resp->len = htons(sz);
	resp->msg_code = code | 0x80;
	struct err_resp *er = (struct err_resp *)resp->data;
	er->err_code = err;
	*msg = resp;
	*len = sz;
}

int phases(calc_req *req)
{
	int ret = 0;
	for (int i = 0; i < 8; i++) {
		uint8_t bit = 0x1 << i;
		if (req->stream[0] & bit)
			ret++;
		if (req->stream[1] & bit)
			ret++;
	}
	return ret;
}

// confirmation = empty msg
void make_confirmation(uint8_t code, void **msg, int *len)
{
	int sz = sizeof(pdu_t);
	pdu_t *resp = malloc(sz);
	resp->msg_code = code;
	resp->len = htons(sz);
	*msg = (void *)resp;
	*len = sz;
}

int parse_request(void *in, int in_len, void **out, int *out_len)
{
	int len;
	if (in_len < sizeof(pdu_t)) {
		emd_log(LOG_DEBUG, "request size is too small!");
		return -1;
	}

	pdu_t *hdr = (pdu_t *)in;
	hdr->len = ntohs(hdr->len);
	int data_len = hdr->len - sizeof(pdu_t);

	switch (hdr->msg_code) {
		case SET_TIME_REQ: {
			if (data_len != sizeof(struct set_time_req)) {
				emd_log(LOG_DEBUG, "SET_TIME_REQ error data size!");
				return -1;
			}				

			struct set_time_req *req = (struct set_time_req *)hdr->data;
			apply_time(ntohl(req->time));
			make_confirmation(hdr->msg_code, out, out_len);
			break;
		}

		case STATE_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "STATE_REQ error data size!");
				return -1;
			}				

			len = sizeof(pdu_t) + sizeof(struct state_resp);
			pdu_t *resp = malloc(len);
			resp->msg_code = hdr->msg_code;
			resp->len = htons(len);
			struct state_resp *s = (struct state_resp *)resp->data;
			// set current state
			int strm1, strm2;
			stream_states(&strm1, &strm2);
			s->streams_state = (strm1? STREAM1_OK: 0) | 
				(strm2? STREAM2_OK: 0);

			*out = (void *)resp;
			*out_len = len;
			break;
		}
		case GET_ADC_PROP_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_ADC_REQ error data size!");
				return -1;
			}
			if (adc_prop_valid) {
				len = sizeof(pdu_t) + sizeof(adc_prop_resp);
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = htons(len);
				adc_prop_resp *data = (adc_prop_resp *)resp->data;
				*data = adc_prop;
				*out = (void *)resp;
				*out_len = len;
			} else
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);

			break;
		} 
		case SET_ADC_PROP_REQ: {
			if (data_len != sizeof(adc_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_ADC_REQ error data size!");
				return -1;
			}
			adc_prop_resp *data = (adc_prop_resp *)hdr->data;
			if (set_adc_prop(data) == -1)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else {
				make_confirmation(hdr->msg_code, out, out_len);
				read_start();
			}
			break;
		}
		case GET_STREAMS_PROP_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_STREAMS_REQ error data size!");
				return -1;
			}
			len = sizeof(pdu_t) + sizeof(streams_prop_resp);
			pdu_t *resp = malloc(len);
			resp->msg_code = hdr->msg_code;
			resp->len = htons(len);
			streams_prop_resp *data = (streams_prop_resp *)resp->data;

			memcpy(data, &streams_prop, sizeof(streams_prop_resp));
			data->u_trans_coef1 = htonl(data->u_trans_coef1);
			data->i_trans_coef1 = htonl(data->i_trans_coef1);
			data->u_trans_coef2 = htonl(data->u_trans_coef2);
			data->i_trans_coef2 = htonl(data->i_trans_coef2);
			*out = (void *)resp;
			*out_len = len;
			break;
		} 
		case SET_STREAMS_PROP_REQ: {
			if (data_len != sizeof(streams_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_STREAMS_PROP_REQ error data size!");
				return -1;
			}
			streams_prop_resp *data = (streams_prop_resp *)hdr->data;
			data->u_trans_coef1 = ntohl(data->u_trans_coef1);
			data->i_trans_coef1 = ntohl(data->i_trans_coef1);
			data->u_trans_coef2 = ntohl(data->u_trans_coef2);
			data->i_trans_coef2 = ntohl(data->i_trans_coef2);
			if (set_streams_prop(data) == -1)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else {
				make_confirmation(hdr->msg_code, out, out_len);
				read_start();
			}

			break;
		}
		case GET_SYNC_PROP_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_SYNC_PROP_REQ error data size!");
				return -1;
			}
			if (sync_prop_valid) {
				len = sizeof(pdu_t) + sizeof(sync_prop_resp);
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = htons(len);
				sync_prop_resp *data = (sync_prop_resp *)resp->data;
				*data = sync_prop;
				*out = (void *)resp;
				*out_len = len;
			} else
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);

			break;
		} 
		case SET_SYNC_PROP_REQ: {
			if (data_len != sizeof(sync_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_SYNC_PROP_REQ error data size!");
				return -1;
			}
			sync_prop_resp *data = (sync_prop_resp *)hdr->data;
			if (set_sync_prop(data) == -1)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else {
				make_confirmation(hdr->msg_code, out, out_len);
			}
			break;
		}
		case GET_CALC_DATA_REQ: {
			if (data_len != sizeof(struct calc_data_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_DATA_REQ error data size!");
				return -1;
			}
			struct calc_data_req *req = (struct calc_data_req *)hdr->data; 

			req->scale = ntohl(req->scale);
			req->begin = ntohl(req->begin);
			req->length = ntohl(req->length);
			req->counts_limit = ntohl(req->counts_limit);

			struct calc_data *cd;
			int cd_size;
			struct timeval ts; 
			make_calc_data(req->idx1, req->idx2, req->scale, req->begin, req->length, req->counts_limit, &ts, &cd, &cd_size);
			if (cd_size == 0)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + cd_size;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = htons(len);
				struct calc_resp *clc = (struct calc_resp *)resp->data;
				clc->ts_sec = ts.tv_sec;
				clc->ts_usec = ts.tv_usec;
				clc->valid1 = cd->size1 != 0;
				clc->valid2 = cd->size2 != 0;

				for (int i = 0; i < (cd->size1 + cd->size2); i++) {
					uint32_t v = htonl(*((uint32_t *)&cd->data[i]));
					cd->data[i] = *(float *)&v;
				}
				cd->size1 = htonl(cd->size1);
				cd->size2 = htonl(cd->size2);
			 
				memcpy((struct calc_general *)clc->data, cd, cd_size);
				free(cd);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALC_COMPARATOR_REQ: {
			if (data_len != sizeof(struct calc_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_REQ error data size!");
				return -1;
			}
			struct calc_req *req = (struct calc_req *)hdr->data; 

			struct calc_comparator cc[PHASES_IN_STREAM*2];
			int ret = make_comparator_calc(req, &cc);
			if (ret < 0)
				make_err_resp(hdr->msg_code, ret, out, out_len);
			else {
				int phs = phases(req);
				len = sizeof(pdu_t) + sizeof(struct calc_comparator_resp) + sizeof(calc_comparator)*phs;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = htons(len);
				struct calc_comparator_resp *ccr = (struct calc_comparator_resp *)resp->data;
				*ccr = *req;
				memcpy(ccr->data, cc, sizeof(calc_comparator)*phs);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALC_POWER_REQ: {
			if (data_len != sizeof(struct calc_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_REQ error data size!");
				return -1;
			}
			struct calc_req *req = (struct calc_req *)hdr->data; 

			struct calc_power cp;
			struct timeval ts; 
			if (make_calc_power(req->idx1, req->idx2, &ts, cp) < 0)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_power);
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = htons(len);
				struct calc_resp *clc = (struct calc_resp *)resp->data;
				clc->ts_sec = ts.tv_sec;	//htobe64(ts.tv_sec);
				clc->ts_usec = ts.tv_usec;	//htobe64(ts.tv_usec);
				clc->valid1 = 1;
				clc->valid2 = 0;
				struct calc_power *cpp = (struct calc_power *)clc->data;
				memcpy(cpp, cp, sizeof(calc_power));
				calc_power_to_be64(cpp);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_VERSION_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_VERSION_REQ error data size!");
				return -1;
			}
			len = sizeof(pdu_t) + sizeof(versions_resp);
			pdu_t *resp = malloc(len);
			resp->msg_code = hdr->msg_code;
			resp->len = htons(len);
			versions_resp *data = (versions_resp *)resp->data;
			strncpy(data->emd, VERSION, VERSION_MAX_LEN);
			strncpy(data->adc, adc_version, VERSION_MAX_LEN);
			strncpy(data->sync, sync_version, VERSION_MAX_LEN);
			*out = (void *)resp;
			*out_len = len;
			break;
		}
		case SET_NETWORK_REQ: {
			if (data_len != sizeof(network)) {
				emd_log(LOG_DEBUG, "SET_NETWORK_REQ error data size!");
				return -1;
			}
			set_network((network *)hdr->data);
			make_confirmation(hdr->msg_code, out, out_len);
			break;
		}

		default:
			return -1;

	}

	return hdr->len;
}

void set_network(const network *net)
{
    char buf[INET_ADDRSTRLEN];

	// adc board
    inc_ip4_addr(buf, net->addr, 1);
	adc_change_network(buf, net->mask);
	
	// sync board
    inc_ip4_addr(buf, net->addr, 2);
	sync_change_network(buf, net->mask, net->gateway);


	// network parameters
	const char *fn = "/etc/conf.d/net"; 
	FILE *fp = fopen(fn, "w+");
	if (!fp) {
		// FIXME restore previos address and etc.
		emd_log(LOG_ERR, "open(%s) failed!: %s", fn, strerror(errno));
		return;
	}
	fprintf(fp, "config_enp1s0=\"null\"\n"
		"bridge_br0=\"enp1s0\"\n"
		"config_br0=\"%s netmask %s\"",
		net->addr, net->mask);
	fflush(fp);
	fclose(fp);

	// udhcpd
	// тупо посчитать [start, end]:
	// if xxx.xxx.xxx.nnn < xxx.xxx.xxx.10
	//	[xxx.xxx.xxx.nnn+3, xxx.xxx.xxx.nnn+3+10]
	// else
	//	[xxx.xxx.xxx.nnn-10, xxx.xxx.xxx.nnn-1]
	// FIXME сделать по маске сети
	char start[INET_ADDRSTRLEN];
	char end[INET_ADDRSTRLEN];
	fn = "/etc/udhcpd.conf";
	char *pbase = strrchr(net->addr, '.');
	int base = atoi(++pbase);
	if (base < 10) {
		inc_ip4_addr(start, net->addr, 3);
		inc_ip4_addr(end, start, 10);
	} else {
		inc_ip4_addr(start, net->addr, -10);
		inc_ip4_addr(end, net->addr, -1);
	}
	
	fp = fopen(fn, "w+");
	if (!fp) {
		// FIXME restore previos address and etc.
		emd_log(LOG_ERR, "open(%s) failed!: %s", fn, strerror(errno));
		return;
	}
	fprintf(fp, "interface br0\n"
		"start %s\n"
		"end %s\n"
		"option subnet %s",
		start, end, net->mask);
	fflush(fp);
	fclose(fp);

	// restart
	system("/etc/init.d/net.br0 restart");
}


void calc_results_to_be64(struct calc_general *cr)
{
	// FIXME Не работает обратное приведение в андроиде
	// Попробовать здесь и в андроиде 
	// 1. добалять по одному элементу
	// 2. использовать промежуточную переменную для
	// результата преобразования
	return;
	uint64_t *v = (uint64_t *)&cr->rms;
	*v = htobe64(*v);
	v = (uint64_t *)&cr->dc;
	*v = htobe64(*v);
	v = (uint64_t *)&cr->f_1h;
	*v = htobe64(*v);
	v = (uint64_t *)&cr->rms_1h;
	*v = htobe64(*v);
	v = (uint64_t *)&cr->phi;
	*v = htobe64(*v);
	v = (uint64_t *)&cr->thd;
	*v = htobe64(*v);
#if 0
	for (int i = 0; i < cr->harmonics_num; i++) {
		v = (uint64_t *)&cr->h[i].f;
		*v = htobe64(*v);
		v = (uint64_t *)&cr->h[i].k;
		*v = htobe64(*v);
		v = (uint64_t *)&cr->h[i].ampl;
		*v = htobe64(*v);
	}
#endif
}

void calc_power_to_be64(struct calc_power *cp)
{
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
	if (correct_time) {
		settimeofday(&tv, NULL);
		correct_time = 0;
	}
}
