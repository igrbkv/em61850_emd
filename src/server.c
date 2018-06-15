#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <endian.h>
#include <stdio.h>
#include <errno.h>
#include <uv.h>

#include "log.h"
#include "tcp_server.h"
#include "sv_read.h"
#include "settings.h"
#include "adc_client.h"
#include "sync_client.h"
#include "proto.h"
#include "calc.h"
#include "streams_list.h"
#include "calib.h"
#include "upgrade.h"
#include "emd.h"
#include "server.h"


static struct proto_ver emd_proto_ver = {
	.major = PROTO_VER_MAJOR,
	.minor = PROTO_VER_MINOR
}; 


#define MAX_DIFF_TIME 2

enum STREAM_STATE {
	SS_INITIAL,		// valid CHECK_PROTO_VER_REQ => SS_AUTH
					// else => SS_DEAD_STATE
	SS_DEAD_STATE,	// only GET_PROTO_VER_REQ 
	SS_AUTH,		// valid CHECK_PIN_CODE_REQ => SS_WORK
	SS_WORK,
};

typedef int (*req_handler_t)(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len);
#define REQ_HANDLER(name) static int name(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len);

#define IS_SS_WORK() if (sd->state != SS_WORK) { \
		make_err_resp(hdr->msg_code, ERR_FORBIDDEN, out, out_len); \
		return hdr->len; \
	}

REQ_HANDLER(unknown_req_handler) 
REQ_HANDLER(check_proto_ver_req_handler) 
REQ_HANDLER(get_proto_ver_req_handler) 
REQ_HANDLER(check_pin_code_req_handler) 
REQ_HANDLER(set_time_req_handler)
REQ_HANDLER(get_state_req_handler)
REQ_HANDLER(get_adc_prop_req_handler)
REQ_HANDLER(set_adc_prop_req_handler)
REQ_HANDLER(set_adc_param_req_handler)
REQ_HANDLER(get_streams_prop_req_handler)
REQ_HANDLER(set_streams_prop_req_handler)
REQ_HANDLER(get_streams_list_req_handler)
REQ_HANDLER(get_sync_prop_req_handler)
REQ_HANDLER(set_sync_prop_req_handler)
REQ_HANDLER(get_version_req_handler)
REQ_HANDLER(set_network_req_handler)
REQ_HANDLER(get_calc_data_req_handler)
REQ_HANDLER(get_calc_comparator_req_handler)
REQ_HANDLER(get_calc_harmonics_req_handler)
REQ_HANDLER(get_calc_ui_req_handler)
REQ_HANDLER(get_calc_p_req_handler)
REQ_HANDLER(get_calc_a_req_handler)
REQ_HANDLER(get_calib_coef_req_handler)
REQ_HANDLER(get_calib_null_req_handler)
REQ_HANDLER(get_calib_scale_req_handler)
REQ_HANDLER(get_calib_angle_req_handler)
REQ_HANDLER(upload_distr_part_req_handler)

struct req_handler {
	req_handler_t handler;
	int data_size;	// -1 - variable
} req_handlers_array[128] = {
	[SUBMIT_PROTO_VER_REQ] = {check_proto_ver_req_handler, sizeof(struct proto_ver)},
	[GET_PROTO_VER_REQ] = {get_proto_ver_req_handler, 0},
	[CHECK_PIN_CODE_REQ] = {check_pin_code_req_handler, sizeof(struct pin)},
	[SET_TIME_REQ] = {set_time_req_handler, sizeof(struct set_time_req)},
	[GET_STATE_REQ] = {get_state_req_handler, 0},
	[GET_ADC_PROP_REQ] = {get_adc_prop_req_handler, 0},
	[SET_ADC_PROP_REQ] = {set_adc_prop_req_handler, sizeof(adc_prop_resp)},
	[SET_ADC_PARAM_REQ] = {set_adc_param_req_handler, sizeof(adc_param_req)},
	[GET_STREAMS_PROP_REQ] = {get_streams_prop_req_handler, 0},
	[SET_STREAMS_PROP_REQ] = {set_streams_prop_req_handler, sizeof(streams_prop_resp)},
	[GET_STREAMS_LIST_REQ] = {get_streams_list_req_handler, 0},
	[GET_SYNC_PROP_REQ] = {get_sync_prop_req_handler, 0},
	[SET_SYNC_PROP_REQ] = {set_sync_prop_req_handler, sizeof(sync_prop_resp)},
	[GET_VERSION_REQ] = {get_version_req_handler, 0},
	[SET_NETWORK_REQ] = {set_network_req_handler, sizeof(network)},
	[GET_CALC_DATA_REQ] = {get_calc_data_req_handler, sizeof(struct calc_data_req)},
	[GET_CALC_COMPARATOR_REQ] = {get_calc_comparator_req_handler, sizeof(struct calc_req)},
	[GET_CALC_HARMONICS_REQ] = {get_calc_harmonics_req_handler, sizeof(struct calc_req)},
	[GET_CALC_UI_REQ] = {get_calc_ui_req_handler, sizeof(struct calc_multimeter_req)},
	[GET_CALC_P_REQ] = {get_calc_p_req_handler, sizeof(struct calc_multimeter_req)},
	[GET_CALC_A_REQ] = {get_calc_a_req_handler, sizeof(struct calc_multimeter_req)},
	[GET_CALIB_COEF_REQ] = {get_calib_coef_req_handler, 0},
	[GET_CALIB_NULL_REQ] = {get_calib_null_req_handler, sizeof(calc_req)},
	[GET_CALIB_SCALE_REQ] = {get_calib_scale_req_handler, sizeof(calc_req)},
	[GET_CALIB_ANGLE_REQ] = {get_calib_angle_req_handler, sizeof(calc_req)},
	[UPLOAD_DISTR_PART_REQ] = {upload_distr_part_req_handler, -1},
};


int correct_time = 1; // Время усанавливается по планшету один раз за сессию
static void make_err_resp(int8_t code, uint8_t err, void **msg, int *len);
static void apply_time(int32_t client_time);
static void set_network(const network *net);
static int valid_proto_ver(const struct proto_ver *pv);

void init_stream_data(struct stream_data *sd)
{
	sd->state = SS_INITIAL;
	sd->fragment.buf = NULL;
	sd->fragment.len = 0;
}

void free_stream_data(struct stream_data *sd)
{
	if (sd->fragment.buf) {
		free(sd->fragment.buf);
		sd->fragment.buf = NULL;
		sd->fragment.len = 0;
	}
}

const char *get_proto_ver()
{
	static char buf[16];
	snprintf(buf, sizeof(buf), "%d.%d", emd_proto_ver.major, emd_proto_ver.minor);
	return buf;
}

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

/* Return:
 *    -1  - error
 *    0   - request was truncated, skip to next chunk
 *    <n> - the length of ready for process request
 */	
int check_in(void *in, int in_len)
{
	if (in_len < sizeof(pdu_t)) {
		emd_log(LOG_DEBUG, "The request size is too small!");
		return 0;
	}

	pdu_t *hdr = (pdu_t *)in;
	if (in_len < hdr->len) {
		if (emd_debug > 1)
			emd_log(LOG_DEBUG, "The request was truncated!");
		return 0;
	}
    
	struct req_handler *rh = &req_handlers_array[hdr->msg_code];
	if (!rh->handler)
		return hdr->len;

	int data_len = hdr->len - sizeof(pdu_t);
	int dsize = req_handlers_array[hdr->msg_code].data_size;
	if (dsize == -1 || dsize == data_len) {
		if (emd_debug > 1) {
			emd_log(LOG_DEBUG, "New request packet_len: %d msg_len: %d code: %d", in_len, hdr->len, hdr->msg_code);
		}
		return hdr->len;
	}

	emd_log(LOG_DEBUG, "Request %u. Error data size!(need:%d, got:%d)", hdr->msg_code, dsize, data_len);
	return -1;
}

int parse_request(void *handle, void *in, int in_len, void **out, int *out_len)
{

	int ret = check_in(in, in_len);
	if (ret <= 0)
		return ret;

	struct stream_data *sd = (struct stream_data *)(((uv_handle_t *)handle)->data);
	pdu_t *hdr = (pdu_t *)in;
	struct req_handler *rh = &req_handlers_array[hdr->msg_code];
	if (rh->handler)
		ret = rh->handler(sd, hdr, out, out_len);
	else
		ret = unknown_req_handler(sd, hdr, out, out_len);

	return ret;
}


int unknown_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	make_err_resp(hdr->msg_code, sd->state == SS_WORK? ERR_UNKNOWN_REQ: ERR_FORBIDDEN, out, out_len);
	return hdr->len; 
}

int check_proto_ver_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	if (sd->state == SS_INITIAL || sd->state == SS_WORK) {
		struct proto_ver *pv = (struct proto_ver *)hdr->data;
		if (sd->state == SS_INITIAL)
			emd_log(LOG_INFO, "Запрос подключения. Версия протокола клиента %d.%d", pv->major, pv->minor);

		if (valid_proto_ver(pv)) {
			make_confirmation(hdr->msg_code, out, out_len);
			if (sd->state == SS_INITIAL)
				sd->state = SS_AUTH;
		} else {
			make_err_resp(hdr->msg_code, ERR_PROTO_VER, out, out_len);
			if (sd->state == SS_INITIAL)
				sd->state = SS_DEAD_STATE;
		}

	} else 
		make_err_resp(hdr->msg_code, ERR_FORBIDDEN, out, out_len);

	return hdr->len;
}
	
int get_proto_ver_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	if (sd->state == SS_WORK || sd->state == SS_DEAD_STATE) {
		int len = sizeof(pdu_t) + sizeof(struct proto_ver);
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct proto_ver *pv = (struct proto_ver *)resp->data;
		*pv = emd_proto_ver;

		*out = (void *)resp;
		*out_len = len;
	} else 
		make_err_resp(hdr->msg_code, ERR_FORBIDDEN, out, out_len);

	return hdr->len;
}

int check_pin_code_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	if (sd->state == SS_WORK || sd->state == SS_AUTH) {
		struct pin *p = (struct pin *)hdr->data;
		if (p->code == emd_pin_code) {
			make_confirmation(hdr->msg_code, out, out_len);
			if (sd->state == SS_AUTH)
				sd->state = SS_WORK;
		}
		else {
			make_err_resp(hdr->msg_code, ERR_AUTH, out, out_len);
		}
	} else 
		make_err_resp(hdr->msg_code, ERR_FORBIDDEN, out, out_len);
	
	return hdr->len;	
}

int set_time_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct set_time_req *req = (struct set_time_req *)hdr->data;
	apply_time(req->time);
	make_confirmation(hdr->msg_code, out, out_len);

	return hdr->len; 
}

int get_state_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	int len = sizeof(pdu_t) + sizeof(struct state_resp);
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
	
	return hdr->len; 
}

int get_adc_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	if (adc_prop_valid) {
		int len = sizeof(pdu_t) + sizeof(adc_prop_resp);
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		adc_prop_resp *data = (adc_prop_resp *)resp->data;
		*data = adc_prop;
		*out = (void *)resp;
		*out_len = len;
	} else
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);

	return hdr->len;
}

int set_adc_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	adc_prop_resp *data = (adc_prop_resp *)hdr->data;
	if (set_adc_prop(data) == -1)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		make_confirmation(hdr->msg_code, out, out_len);
		read_start();
	}

	return hdr->len;
}

int set_adc_param_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	adc_param_req *data = (adc_param_req *)hdr->data;
	if (set_adc_param(data) == -1)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		make_confirmation(hdr->msg_code, out, out_len);
		switch (data->type) {
			case ADC_PARAM_TYPE_SRC_MAC:
			case ADC_PARAM_TYPE_DST_MAC:
			case ADC_PARAM_TYPE_SV_ID:
				// pcap restart
				read_start();
				break;
		}
	}

	return hdr->len;
}

int get_streams_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	int len = sizeof(pdu_t) + sizeof(streams_prop_resp);
	pdu_t *resp = malloc(len);
	resp->msg_code = hdr->msg_code;
	resp->len = len;
	streams_prop_resp *spr = (streams_prop_resp *)resp->data;

	memcpy(spr, &streams_prop, sizeof(streams_prop_resp));
	spr->timeout = sv_timeout_ms;
	spr->threshold = sv_threshold_ms;

	*out = (void *)resp;
	*out_len = len;

	return hdr->len;
}

int set_streams_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	streams_prop_resp *spr = (streams_prop_resp *)hdr->data;
	if (set_streams_prop(spr) == -1)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		make_confirmation(hdr->msg_code, out, out_len);
		read_start();
	}

	return hdr->len;
}

int get_streams_list_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	stream_property *sp;
	int ret = scan_streams(&sp);
	if (ret == -1)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(streams_list) + sizeof(stream_property)*ret;
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

	return hdr->len;
}

int get_sync_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	if (sync_prop_valid) {
		int len = sizeof(pdu_t) + sizeof(sync_prop_resp);
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		sync_prop_resp *data = (sync_prop_resp *)resp->data;
		*data = sync_prop;
		*out = (void *)resp;
		*out_len = len;
	} else
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);

	return hdr->len;
}

int set_sync_prop_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	sync_prop_resp *data = (sync_prop_resp *)hdr->data;
	if (set_sync_prop(data) == -1)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		make_confirmation(hdr->msg_code, out, out_len);
	}

	return hdr->len;
}

int get_version_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	int len = sizeof(pdu_t) + sizeof(versions_resp);
	pdu_t *resp = malloc(len);
	resp->msg_code = hdr->msg_code;
	resp->len = len;
	versions_resp *data = (versions_resp *)resp->data;
	strncpy(data->distr, get_distr_version(), VERSION_MAX_LEN);
	strncpy(data->adc, adc_version, VERSION_MAX_LEN);
	strncpy(data->sync, sync_version, VERSION_MAX_LEN);
	*out = (void *)resp;
	*out_len = len;
	
	return hdr->len;
}

int set_network_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	set_network((network *)hdr->data);
	make_confirmation(hdr->msg_code, out, out_len);

	return hdr->len;
}

int get_calc_data_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	calc_data_req *req = (struct calc_data_req *)hdr->data; 

	calc_data *cd;
	int cd_size;
	int ret = make_calc_data(req, &cd, &cd_size);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + cd_size;
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

	return hdr->len;
}

int get_calc_comparator_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_req *req = (struct calc_req *)hdr->data; 
	struct calc_comparator cc[PHASES_IN_STREAM*2];
	int phs;
	int ret = make_comparator_calc(req, cc, &phs);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_comparator)*phs;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = *req;
		memcpy(cr->data, cc, sizeof(calc_comparator)*phs);

		*out = (void *)resp;
		*out_len = len;
	}

	return hdr->len;
}

int get_calc_harmonics_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_req *req = (struct calc_req *)hdr->data; 

	char buf[PHASES_IN_STREAM*2*(sizeof(calc_harmonics) + sizeof(struct calc_harmonic)*harmonics_count)];
	int sz;
	int ret = make_calc_harm(req,(calc_harmonics *)buf, &sz);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sz;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = *req;
		memcpy(cr->data, buf, sz);

		*out = (void *)resp;
		*out_len = len;
	}

	return hdr->len;
}

int get_calc_ui_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

	struct calc_ui cui[PHASES_IN_STREAM*2];
	calc_ui_diff cui_diff[PHASES_IN_STREAM*2];
	int phs, ds;
	int ret = make_calc_ui(req, cui, &phs, cui_diff, &ds);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_ui)*phs + sizeof(calc_ui_diff)*ds;
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

	return hdr->len;
}

int get_calc_p_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

	struct calc_p cp[PHASES_IN_STREAM/2*2];
	int ret = make_calc_p(req, cp);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_p)*ret;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = req->req;
		memcpy(cr->data, cp, sizeof(calc_p)*ret);

		*out = (void *)resp;
		*out_len = len;
	}

	return hdr->len;
}

int get_calc_a_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_multimeter_req *req = (struct calc_multimeter_req *)hdr->data; 

	calc_a ca[PHASES_IN_STREAM*2];
	int ret = make_calc_a(req, ca);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_a)*ret;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = req->req;
		memcpy(cr->data, ca, sizeof(calc_a)*ret);

		*out = (void *)resp;
		*out_len = len;
	}

	return hdr->len;
}

int get_calib_coef_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	if (!adc_prop_valid)
		make_err_resp(hdr->msg_code, ERR_NOT_AVAILABLE, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(adc_corrs);
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		memcpy(resp->data, &adc_corrs, sizeof(adc_corrs));
		*out = (void *)resp;
		*out_len = len;
	}
	
	return hdr->len;
}

int get_calib_null_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_req *req = (struct calc_req *)hdr->data;
	struct dvalue vals[PHASES_IN_STREAM];
	int ret = make_calib_null(req, vals);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_a)*ret;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = *req;
		memcpy(cr->data, vals, sizeof(struct dvalue)*ret);

		*out = (void *)resp;
		*out_len = len;
	}
	
	return hdr->len;
}

int get_calib_scale_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_req *req = (struct calc_req *)hdr->data;
	struct dvalue vals[PHASES_IN_STREAM];
	int ret = make_calib_scale(req, vals);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calc_a)*ret;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = *req;
		memcpy(cr->data, vals, sizeof(struct dvalue)*ret);

		*out = (void *)resp;
		*out_len = len;
	}
	
	return hdr->len;
}


int get_calib_angle_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	IS_SS_WORK();

	struct calc_req *req = (struct calc_req *)hdr->data; 

	calib_angle vals[PHASES_IN_STREAM];
	int	ret = make_calib_angle(req, vals);
	if (ret < 0)
		make_err_resp(hdr->msg_code, -ret, out, out_len);
	else {
		int len = sizeof(pdu_t) + sizeof(struct calc_resp) + sizeof(calib_angle)*ret;
		pdu_t *resp = malloc(len);
		resp->msg_code = hdr->msg_code;
		resp->len = len;
		struct calc_resp *cr = (struct calc_resp *)resp->data;
		cr->resp = *req;
		memcpy(cr->data, vals, sizeof(calib_angle)*ret);

		*out = (void *)resp;
		*out_len = len;
	}
	
	return hdr->len;
}

int upload_distr_part_req_handler(struct stream_data *sd, pdu_t *hdr, void **out, int *out_len)
{
	//IS_SS_WORK();

	int ret = save_distr_part(sd, hdr->len - sizeof(pdu_t), hdr->data);
	if (ret < 0)
		make_err_resp(hdr->msg_code, ERR_INTERNAL, out, out_len);
	else
		make_confirmation(hdr->msg_code, out, out_len);

	return hdr->len;
}

void set_network(const network *net)
{
    char buf[INET_ADDRSTRLEN];

	// udhcpd
	// тупо посчитать [start, end]:
	// if xxx.xxx.xxx.nnn < xxx.xxx.xxx.241
	//	[xxx.xxx.xxx.nnn+3, xxx.xxx.xxx.nnn+3+10]
	// else
	//	выход без изменений
	//
	// FIXME сделать по маске сети
	char start[INET_ADDRSTRLEN];
	char end[INET_ADDRSTRLEN];
	char *pbase = strrchr(net->addr, '.');
	int base = atoi(++pbase);
	if (base < 241) {
		inc_ip4_addr(start, net->addr, 3);
		inc_ip4_addr(end, start, 10);
	} else {
		emd_log(LOG_ERR, "Невозможно выделить диапазон адресов для dhcp сервера");
		return;
	}

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

	// 
	for (int i = 0; i < emd_eth_ifaces_count; i++)
		fprintf(fp,	"config_eth%d=\"null\"\n", i);

	fprintf(fp,	"bridge_br0=\"");
	for (int i = 0; i < emd_eth_ifaces_count; i++)
		fprintf(fp,	"%seth%d", i == 0? "": " ", i);

	fprintf(fp,	"\"\nconfig_br0=\"%s netmask %s\"",
		net->addr, net->mask);

	fflush(fp);
	fclose(fp);

	// change dhcp config file
	fn = "/etc/udhcpd.conf";
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


int valid_proto_ver(const struct proto_ver *pv)
{
	return pv->major == emd_proto_ver.major &&
		pv->minor <= emd_proto_ver.minor;
}
