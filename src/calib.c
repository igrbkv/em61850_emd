#include <stdlib.h>
#include <string.h>

#include "proto.h"
#include "adc_client.h"
#include "calib.h"
#include "calc.h"
#include "calc_math.h"

#include "debug.h"

static calc_req last_req;

static int calib_null_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals);
static int calib_scale_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals);
static int calib_angle_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals);

int make_calib_null(calc_req *req, struct dvalue *vals)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};

	if (req->stream[0] == 0 && req->stream[1] == 0)
		return 0;

	sv_get_ready(&req->time_stamp, req->stream[0]? &svd[0]: NULL, req->stream[0]? &svd_size[0]: NULL, req->stream[1]? &svd[1]: NULL, req->stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, req, sizeof(calc_req)) == 0)
		return -ERR_RETRY;

	last_req = *req;

	if (svd_size[0] == 0)
		req->stream[0] = 0;
	if (svd_size[1] == 0)
		req->stream[1] = 0;

	// no data
	if (svd_size[0] == 0 && svd_size[1] == 0)
		return 0;

	int ps = 0;
	for (int i = 0; i < 2; i++)
		if (svd_size[i]) {
			set_stream_values(i, req->stream[i], svd[i], svd_size[i]);
			prepare_phases(i, req->stream[i]);
			ps += calib_null_stream(i, req->stream[i], &vals[ps]);
		}
	return ps;
}

int calib_null_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals)
{
	int count = 0;
	struct dvalue *_vals = vals;
	calc_stream *stm = stream[stm_idx];

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (BIT(phases_mask, p)) {
			phase *ph = &stm->phases[p];
			double	mean_wh = 0.0;

			for (int i = 0; i < stm->v_size; i++) {
				double val = ph->values[i];
				double hf = stm->hanning_full[i];

				mean_wh += hf * val;
			}
			_vals->value = mean_wh;
			_vals++;
			count++;
		}
	}

	return count;
}

int make_calib_scale(calc_req *req, struct dvalue *vals)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};

	if (req->stream[0] == 0 && req->stream[1] == 0)
		return 0;

	sv_get_ready(&req->time_stamp, req->stream[0]? &svd[0]: NULL, req->stream[0]? &svd_size[0]: NULL, req->stream[1]? &svd[1]: NULL, req->stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, req, sizeof(calc_req)) == 0)
		return -ERR_RETRY;

	last_req = *req;

	if (svd_size[0] == 0)
		req->stream[0] = 0;
	if (svd_size[1] == 0)
		req->stream[1] = 0;

	// no data
	if (svd_size[0] == 0 && svd_size[1] == 0)
		return 0;


	int ps = 0;
	for (int i = 0; i < 2; i++)
		if (svd_size[i]) {
			set_stream_values(i, req->stream[i], svd[i], svd_size[i]);
#ifndef RMS_WH
			// only for rms_1h
			prepare_phases(i, req->stream[i]);
#endif
			ps += calib_scale_stream(i, req->stream[i], &vals[ps]);
		}
	return ps;
}

int calib_scale_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals)
{
	int count = 0;
	struct dvalue *_vals = vals;
	calc_stream *stm = stream[stm_idx];

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (BIT(phases_mask, p)) {
			phase *ph = &stm->phases[p];
#ifdef RMS_WH
			double	rms_wh = 0.0;

			for (int i = 0; i < stm->v_size; i++) {
				double val = ph->values[i];
				double hf = stm->hanning_full[i];

				rms_wh += hf * val *val;
			}
			_vals->value = sqrt(rms_wh);

#else // RMS_1H
			double rms_1h = 0.0,
				t_samp = 1./stm->counts;
			double ar[3];
			rev_win_han_scan(ph->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);
			rms_1h = ar[2];
			_vals->value  = rms_1h;
#endif
			_vals++;
			count++;
		}
	}

	return count;
}

int make_calib_angle(calc_req *req, struct dvalue *vals)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};

	if (req->stream[0] == 0 && req->stream[1] == 0)
		return 0;

	sv_get_ready(&req->time_stamp, req->stream[0]? &svd[0]: NULL, req->stream[0]? &svd_size[0]: NULL, req->stream[1]? &svd[1]: NULL, req->stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, req, sizeof(calc_req)) == 0)
		return -ERR_RETRY;

	last_req = *req;

	if (svd_size[0] == 0)
		req->stream[0] = 0;
	if (svd_size[1] == 0)
		req->stream[1] = 0;

	// no data
	if (svd_size[0] == 0 && svd_size[1] == 0)
		return 0;

	int ps = 0;
	for (int i = 0; i < 2; i++)
		if (svd_size[i]) {
			set_stream_values(i, req->stream[i], svd[i], svd_size[i]);
			prepare_phases(i, req->stream[i]);
			ps += calib_angle_stream(i, req->stream[i], &vals[ps]);
		}

	return ps;
}

int calib_angle_stream(int stm_idx, uint8_t phases_mask, struct dvalue *vals)
{
	int count = 0;
	struct dvalue *_vals = vals;
	calc_stream *stm = stream[stm_idx];

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (BIT(phases_mask, p)) {
			phase *ph = &stm->phases[p];

			double t_samp = 1./stm->counts;
			// rev_win_han
			double ar[3];

			int i_max = rev_win_han_scan(ph->ampl_spectre, 3, stm->counts /2 - 1, ar, stm->counts, t_samp);

			double abs_phi = calc_abs_phi(ph->data_complex_out, t_samp, i_max, stm->counts);

			_vals->value = abs_phi;
			
			_vals++;
			count++;
		}
	}
	return count;
}
