#include <stdlib.h>
#include <string.h>
#include "proto.h"
#include "sv_read.h"
#include "calc.h"

static calc_data_req last_req;

// @param  cd: values of signals
// @param  cd_size: длина данных
void make_calc_data(calc_data_req *req, calc_data **data, int *data_size)
{
	sv_data *svd[2];
	int svd_size[2] = {0};
	calc_data_req resp = *req;
	void *_data = 0;
	int _data_size = 0;
	*data = NULL;
	data_size = 0;

	sv_get_ready(&resp.time_stamp, resp.stream[0]? &svd[0]: NULL, resp.stream[0]? &svd_size[0]: NULL, resp.stream[1]? &svd[1]: NULL, resp.stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, &resp, sizeof(calc_data_req)) == 0)
		return -ERR_RETRY;

	last_req = resp;

	if (svd_size[0])
		resp.stream[0] = 0;
	if (svd_size[1])
		resp.stream[1] = 0;

	// no data
	if (svd_size[0] == 0 && svd_size[1] == 0)
		return 0;

	if (svd_size[0]) {
		for (int p = 0; p < PHASES_IN_STREAM; p++) {
			if (resp.stream[0] & (0x1<<p))		
				add_calc_data(data, data_size, p, svd[0], svd_size[0], req->scale, req->begin, req->length, req->counts_limit)
		}
	}

	if (svd_size[1]) {
		for (int p = 0; p < PHASES_IN_STREAM; p++) {
			if (resp.stream[0] & (0x1<<p))		
				add_calc_data(data, data_size, p, svd[0], svd_size[0], req->scale, req->begin, req->length, req->counts_limit)
		}

	}

	return 0;
}

void add_calc_data(void **cd, int *cd_size, int idx, sv_data *s, int s_size, uint32_t scale, uint32_t begin, uint32_t length, uint32_t counts_limit)
{
	double sf = scale_factor(idx);
	int first = s_size/scale*begin;
	int step = 1;
	int counts = length*s_size/scale;
	// шаг прореживания
	while (counts/step > counts_limit)
		step++;
	counts /= step;
	int new_cd_size = *cd_size + sizeof(calc_data) + sizeof(float) * counts;
	*cd = realloc(*cd, new_cd_size);
	calc_data *tail = &(*cd)[*cd_size];
	*cd_size = new_cd_size;
	tail->counts = counts;
	for (int i = 0; i < counts; i++) {
		int v = s[first + i*step].values[idx*2];
		tail->data[i] = sf*(float)v;
	}
}
