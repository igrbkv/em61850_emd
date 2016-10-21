#include <stdlib.h>
#include <string.h>
#include "proto.h"
#include "sv_read.h"
#include "calc.h"

static calc_data_req last_req;

static void add_calc_data(void **cd, int *cd_size, uint8_t phases_mask, sv_data *s, int s_size, uint32_t scale, uint32_t begin, uint32_t length, uint32_t counts_limit);

// @param  cd: values of signals
// @param  cd_size: длина данных
int make_calc_data(calc_data_req *req, calc_data **data, int *data_size)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};
	calc_req *resp = &req->req;
	*data = NULL;
	data_size = 0;

	if (resp->stream[0] == 0 && resp->stream[1] == 0)
		return 0;

	sv_get_ready(&resp->time_stamp, resp->stream[0]? &svd[0]: NULL, resp->stream[0]? &svd_size[0]: NULL, resp->stream[1]? &svd[1]: NULL, resp->stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, req, sizeof(calc_data_req)) == 0)
		return -ERR_RETRY;

	last_req = *req;

	if (svd_size[0] == 0)
		resp->stream[0] = 0;
	if (svd_size[1] == 0)
		resp->stream[1] = 0;

	// no data
	if (svd_size[0] == 0 && svd_size[1] == 0)
		return 0;

	if (svd_size[0])
		add_calc_data((void **)data, data_size, resp->stream[0], svd[0], svd_size[0], req->scale, req->begin, req->length, req->counts_limit);

	if (svd_size[1])
		add_calc_data((void **)data, data_size, resp->stream[1], svd[1], svd_size[1], req->scale, req->begin, req->length, req->counts_limit);

	return 0;
}

void add_calc_data(void **cd, int *cd_size, uint8_t phases_mask, sv_data *s, int s_size, uint32_t scale, uint32_t begin, uint32_t length, uint32_t counts_limit)
{
	int first = s_size/scale*begin;
	int step = 1;
	int counts = length*s_size/scale;
	// шаг прореживания
	while (counts/step > counts_limit)
		step++;
	counts /= step;

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {		
			double sf = scale_factor(p);
			int new_cd_size = *cd_size + sizeof(calc_data) + sizeof(float) * counts;
			*cd = realloc(*cd, new_cd_size);
			calc_data *tail = &(*cd)[*cd_size];
			*cd_size = new_cd_size;
			tail->counts = counts;
			for (int i = 0; i < counts; i++) {
				int v = s[first + i*step].values[p*2];
				tail->data[i] = sf*(float)v;
			}
		}
	}
}
