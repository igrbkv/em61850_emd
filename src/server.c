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
#include "streams_list.h"

#define MAX_DIFF_TIME 2

int correct_time = 1; // Время усанавливается по планшету один раз за сессию
static void make_err_resp(int8_t code, uint8_t err, void **msg, int *len);
static void apply_time(int32_t client_time);
static void set_network(const network *net);

void make_err_resp(int8_t code, uint8_t err, void **msg, int *len)
{
	int sz = sizeof(pdu_t) + sizeof(struct err_resp);
	pdu_t *resp = malloc(sz);
	resp->len = sz;
	resp->msg_code = code | 0x80;
	struct err_resp *er = (struct err_resp *)resp->data;
	er->err_code = err;
	*msg = resp;
	*len = sz;
}

// confirmation = empty msg
void make_confirmation(uint8_t code, void **msg, int *len)
{
	int sz = sizeof(pdu_t);
	pdu_t *resp = malloc(sz);
	resp->msg_code = code;
	resp->len = sz;
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
	int data_len = hdr->len - sizeof(pdu_t);

	switch (hdr->msg_code) {
		case SET_TIME_REQ: {
			if (data_len != sizeof(struct set_time_req)) {
				emd_log(LOG_DEBUG, "SET_TIME_REQ error data size!");
				return -1;
			}				

			struct set_time_req *req = (struct set_time_req *)hdr->data;
			apply_time(req->time);
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
			resp->len = len;
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
				resp->len = len;
				adc_prop_resp *data = (adc_prop_resp *)resp->data;
				*data = adc_prop;
				*out = (void *)resp;
				*out_len = len;
			} else
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);

			break;
		} 
		case SET_ADC_PROP_REQ: {
			if (data_len != sizeof(adc_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_ADC_REQ error data size!");
				return -1;
			}
			adc_prop_resp *data = (adc_prop_resp *)hdr->data;
			if (set_adc_prop(data) == -1)
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
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
			resp->len = len;
			streams_prop_resp *spr = (streams_prop_resp *)resp->data;

			memcpy(spr, &streams_prop, sizeof(streams_prop_resp));
			*out = (void *)resp;
			*out_len = len;
			break;
		} 
		case SET_STREAMS_PROP_REQ: {
			if (data_len != sizeof(streams_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_STREAMS_PROP_REQ error data size!");
				return -1;
			}
			streams_prop_resp *spr = (streams_prop_resp *)hdr->data;
			if (set_streams_prop(spr) == -1)
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
			else {
				make_confirmation(hdr->msg_code, out, out_len);
				read_start();
			}

			break;
		}
		case GET_STREAMS_LIST_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_STREAMS_LIST_REQ error data size!");
				return -1;
			}
			stream_property *sp;
			int ret = scan_streams(&sp);
			if (ret == -1)
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(streams_list) + sizeof(stream_property)*ret;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				streams_list *sl = (streams_list *)resp->data;
				sl->count = ret;
				if (ret)
					memcpy(sl->data, sp, sizeof(stream_property)*ret);
				*out = (void *)resp;
				*out_len = len;
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
				resp->len = len;
				sync_prop_resp *data = (sync_prop_resp *)resp->data;
				*data = sync_prop;
				*out = (void *)resp;
				*out_len = len;
			} else
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);

			break;
		} 
		case SET_SYNC_PROP_REQ: {
			if (data_len != sizeof(sync_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_SYNC_PROP_REQ error data size!");
				return -1;
			}
			sync_prop_resp *data = (sync_prop_resp *)hdr->data;
			if (set_sync_prop(data) == -1)
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
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
			calc_data_req *req = (struct calc_data_req *)hdr->data; 

			calc_data *cd;
			int cd_size;
			int ret = make_calc_data(req, &cd, &cd_size);
			if (ret < 0)
				make_err_resp(hdr->msg_code, -ret, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + cd_size;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				struct calc_resp *cr = (struct calc_resp *)resp->data;
				cr->resp = req->req;
		 
				memcpy(cr->data, cd, cd_size);
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
			int phs;
			int ret = make_comparator_calc(req, cc, &phs);
			if (ret < 0)
				make_err_resp(hdr->msg_code, -ret, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_comparator)*phs;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				struct calc_resp *cr = (struct calc_resp *)resp->data;
				cr->resp = *req;
				memcpy(cr->data, cc, sizeof(calc_comparator)*phs);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALC_UI_REQ: {
			if (data_len != sizeof(struct calc_multimeter_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_UI_REQ error data size!");
				return -1;
			}
			struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

			struct calc_ui cui[PHASES_IN_STREAM*2];
			calc_ui_diff cui_diff[PHASES_IN_STREAM*2];
			int phs, ds;
			int ret = make_calc_ui(req, cui, &phs, cui_diff, &ds);
			if (ret < 0)
				make_err_resp(hdr->msg_code, -ret, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_ui)*phs + sizeof(calc_ui_diff)*ds;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				struct calc_resp *cr = (struct calc_resp *)resp->data;
				cr->resp = req->req;
				memcpy(cr->data, cui, sizeof(calc_ui)*phs);

				memcpy(&cr->data[sizeof(calc_ui)*phs], cui_diff, sizeof(calc_ui_diff)*ds);
				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALC_P_REQ: {
			if (data_len != sizeof(struct calc_multimeter_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_P_REQ error data size!");
				return -1;
			}
			struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

			struct calc_p cp[PHASES_IN_STREAM/2*2];
			int ret = make_calc_p(req, cp);
			if (ret < 0)
				make_err_resp(hdr->msg_code, -ret, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_p)*ret;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				struct calc_resp *cr = (struct calc_resp *)resp->data;
				cr->resp = req->req;
				memcpy(cr->data, cp, sizeof(calc_p)*ret);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALC_A_REQ: {
			if (data_len != sizeof(struct calc_multimeter_req)) {
				emd_log(LOG_DEBUG, "GET_CALC_A_REQ error data size!");
				return -1;
			}
			struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

			calc_a ca[PHASES_IN_STREAM*2];
			int ret = make_calc_a(req, ca);
			if (ret < 0)
				make_err_resp(hdr->msg_code, -ret, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_a)*ret;
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				struct calc_resp *cr = (struct calc_resp *)resp->data;
				cr->resp = req->req;
				memcpy(cr->data, ca, sizeof(calc_a)*ret);

				*out = (void *)resp;
				*out_len = len;
			}
			break;
		} 

		case GET_CALIB_COEF_REQ: {
			if (data_len != 0) {
				emd_log(LOG_DEBUG, "GET_CALIB_COEF_REQ error data size!");
				return -1;
			}

			if (!adc_prop_valid)
				make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
			else {
				len = sizeof(pdu_t) + sizeof(adc_coefs);
				pdu_t *resp = malloc(len);
				resp->msg_code = hdr->msg_code;
				resp->len = len;
				memcpy(resp->data, adc_coefs, sizeof(adc_coefs));
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
			resp->len = len;
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
	fprintf(fp, "config_%s=\"null\"\n"
		"bridge_br0=\"%s\"\n"
		"config_br0=\"%s netmask %s\"",
		emd_interface_name, emd_interface_name, net->addr, net->mask);
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
