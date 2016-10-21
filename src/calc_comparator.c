#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"
#include "calc_math.h"

#include "debug.h"
#ifdef DEBUG
#include <stdio.h>
#endif

static calc_req last_req;

static void calc_comparator_stream(int stm_idx, uint8_t phase_mask, calc_comparator *cmpr);

// @param req: req => resp
// @param cmpr: calc_comparator's ptr
// @return 0  - Ok
//         <0 - error
int make_comparator_calc(calc_req *req, calc_comparator *cmpr)
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

	if (svd_size[0]) {
		set_stream_values(0, req->stream[0], svd[0], svd_size[0]);
		prepare_phases(0, req->stream[0]);
		calc_comparator_stream(0, req->stream[0], cmpr);
	}

	if (svd_size[1]) {
		set_stream_values(0, req->stream[0], svd[0], svd_size[0]);
		prepare_phases(0, req->stream[0]);
		calc_comparator_stream(0, req->stream[0], cmpr);
	}

	return 0;
}


void calc_comparator_stream(int stm_idx, uint8_t phases_mask, calc_comparator *cmpr)
{
	calc_stream *stm = stream[stm_idx];
	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];

			double rect_mean_h = 0.0,
				rms_wh = 0.0,
				mean_wh = 0.0,
				ampl = ph->values[0],
				t_samp = 1./stm->counts;

			for (int i = 0; i < stm->v_size; i++) {
				double val = ph->values[i];
				double hf = stm->hanning_full[i];

				rect_mean_h += fabs(hf * val);
				rms_wh += hf * val * val;		
				mean_wh += hf * val;
				if (val > ampl)
					ampl = val;
			}
			rms_wh = sqrt(rms_wh);

			// rev_win_han
			double ar[3];

			int i_max = rev_win_han_scan(ph->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);

			double abs_phi = calc_abs_phi(ph->data_complex_out, t_samp, i_max, stm->counts);

			ar[ 0 ] *= Kf;
			
			const int max_harm_calc = round(1.0 / ( 2 * t_samp * ar[ 0 ] ) - 0.5);
			const int max_harm = (max_harm_calc < harmonics_count ? max_harm_calc : harmonics_count);

			double thd = 0.;

			for	(int i = 2; i < max_harm; i++) {
				int idx = stm->counts * t_samp * ar[0] * i + 0.5;
				
				double ar_cur[3];		
				i_max = rev_win_han_scan(ph->ampl_spectre, idx - 1, idx + 1, ar_cur, stm->counts, t_samp);
#if 0
				h[i-2].f = ar_cur[0];
				h[i-2].k = ar_cur[1];
				h[i-2].ampl = ar_cur[2];
#endif
				thd += pow(ar_cur[2], 2);
			}
			thd = sqrt(thd) / ar[2];

#if 0
			harmonics_num = max_harm < 2? 0: max_harm - 2;
#endif

			cmpr->rms = rms_wh;
			cmpr->dc = mean_wh;
			cmpr->f_1h = ar[0];
			cmpr->rms_1h = ar[2];
			cmpr->phi = abs_phi;
			cmpr->thd = thd;
			
			cmpr++;
		}
	}
}
