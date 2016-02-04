#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "log.h"
#include "sv_read.h"
#include "settings.h"
#include "adc_client.h"
#include "proto.h"

#define MAX_DIFF_TIME 2


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

// confirmation = empty msg
void make_confirmation(uint8_t code, void **msg, int *len)
{
	pdu_t *resp = malloc(sizeof(pdu_t));
	resp->msg_code = code;
	resp->data_len = htons(0);
	*msg = (void *)resp;
	*len = sizeof(pdu_t);
}

int parse_request(void *in, int in_len, void **out, int *out_len)
{
	if (in_len < sizeof(pdu_t)) {
		emd_log(LOG_DEBUG, "request size is too small!");
		return -1;
	}

	pdu_t *hdr = (pdu_t *)in;
	hdr->data_len = ntohs(hdr->data_len);

	switch (hdr->msg_code) {
		case SET_TIME_REQ: {
			if (hdr->data_len != sizeof(struct set_time_req)) {
				emd_log(LOG_DEBUG, "SET_TIME_REQ error data size!");
				return -1;
			}				

			struct set_time_req *req = (struct set_time_req *)hdr->data;
			apply_time(ntohl(req->time));
			make_confirmation(hdr->msg_code, out, out_len);
			break;
		}

		case STATE_REQ: {
			if (hdr->data_len != 0) {
				emd_log(LOG_DEBUG, "STATE_REQ error data size!");
				return -1;
			}				

			pdu_t *resp = malloc(sizeof(pdu_t) + sizeof(struct state_resp));
			resp->msg_code = hdr->msg_code;
			resp->data_len = htons(sizeof(struct state_resp));
			struct state_resp *s = (struct state_resp *)resp->data;
			// set current state
			
			*out = (void *)resp;
			*out_len = sizeof(pdu_t) + sizeof(struct state_resp);
			break;
		}
		case GET_MAC_REQ: {
			if (hdr->data_len != 0) {
				emd_log(LOG_DEBUG, "GET_MAC_REQ error data size!");
				return -1;
			}
			if (emd_mac[0] != '\0') {
				pdu_t *resp = malloc(sizeof(pdu_t) + sizeof(struct mac_resp));
				resp->msg_code = hdr->msg_code;
				resp->data_len = htons(sizeof(struct mac_resp));
				struct mac_resp *data = (struct mac_resp *)resp->data;
				memcpy(data->mac, emd_mac, 17); 
				*out = (void *)resp;
				*out_len = sizeof(pdu_t) + sizeof(struct mac_resp);
			} else
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);

			break;
		}
		case GET_ADC_PROP_REQ: {
			if (hdr->data_len != 0) {
				emd_log(LOG_DEBUG, "GET_ADC_REQ error data size!");
				return -1;
			}
			if (adc_prop_valid) {
				pdu_t *resp = malloc(sizeof(pdu_t) + sizeof(adc_prop_resp));
				resp->msg_code = hdr->msg_code;
				resp->data_len = htons(sizeof(adc_prop_resp));
				adc_prop_resp *data = (adc_prop_resp *)resp->data;
				*data = adc_prop;
				*out = (void *)resp;
				*out_len = sizeof(pdu_t) + sizeof(adc_prop_resp);
			} else
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);

			break;
		} 
		case SET_ADC_PROP_REQ: {
			if (hdr->data_len != sizeof(adc_prop_resp)) {
				emd_log(LOG_DEBUG, "SET_ADC_REQ error data size!");
				return -1;
			}
			adc_prop_resp *data = (adc_prop_resp *)hdr->data;
			if (set_adc_prop(data) == -1)
				make_err_resp(hdr->msg_code, NOT_AVAILABLE, out, out_len);
			else
				make_confirmation(hdr->msg_code, out, out_len);
			break;
		}
		case GET_STREAMS_PROP_REQ: {
			if (hdr->data_len != 0) {
				emd_log(LOG_DEBUG, "GET_STREAMS_REQ error data size!");
				return -1;
			}
			
			pdu_t *resp = malloc(sizeof(pdu_t) + sizeof(streams_prop_resp));
			resp->msg_code = hdr->msg_code;
			resp->data_len = htons(sizeof(streams_prop_resp));
			streams_prop_resp *data = (streams_prop_resp *)resp->data;

			memcpy(data, &streams_prop, sizeof(streams_prop_resp));
			data->u_trans_coef1 = htonl(data->u_trans_coef1);
			data->i_trans_coef1 = htonl(data->i_trans_coef1);
			data->u_trans_coef2 = htonl(data->u_trans_coef2);
			data->i_trans_coef2 = htonl(data->i_trans_coef2);
			*out = (void *)resp;
			*out_len = sizeof(pdu_t) + sizeof(streams_prop_resp);
			break;
		} 
		case SET_STREAMS_PROP_REQ: {
			if (hdr->data_len != sizeof(streams_prop_resp)) {
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
			else
				make_confirmation(hdr->msg_code, out, out_len);

			break;
		}
	}

	return sizeof(pdu_t) + hdr->data_len;;
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
