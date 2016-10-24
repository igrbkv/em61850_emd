#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"

#include "debug.h"

static calc_req last_req;

static void calc_ui_stream(int stm_idx, uint8_t phases_mask, calc_ui **cui);
static void calc_ui_diff_stream(int stm_idx, uint8_t phases_mask, calc_ui **cui_diff);

// @param req: req => resp
// @param cmpr: calc_comparator's ptr
// @return 0  - Ok
//         <0 - error
int make_calc_ui(calc_req *req, calc_ui *cui, calc_ui_diff *cui_diff)
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

	calc_ui **_cui = &cui;

	if (svd_size[0]) {
		set_stream_values(0, req->stream[0], svd[0], svd_size[0]);
		prepare_phases(0, req->stream[0]);
		calc_ui_stream(0, req->stream[0], _cui);
	}

	if (svd_size[1]) {
		set_stream_values(1, req->stream[1], svd[1], svd_size[1]);
		prepare_phases(1, req->stream[1]);
		calc_ui_stream(1, req->stream[1], _cui);
	}

	calc_ui **_cui_diff = &cui_diff;
	// FIXME потоки могут быть разной частоты дискретизации
	// поэтому считаются отдельно
	if (svd_size[0])
		calc_ui_diff_stream(0, req->stream[0], _cui_diff);
	if (svd_size[1])
		calc_ui_diff_stream(1, req->stream[1], _cui_diff);

	return 0;
}

void calc_ui_stream(int stm_idx, uint8_t phases_mask, calc_ui **cui)
{
	calc_stream *stm = stream[stm_idx];
	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];

			double rms_wh = 0.0,
				mean_wh = 0.0,
				t_samp = 1./stm->counts;

			for (int i = 0; i < stm->v_size; i++) {
				double val = ph->values[i];
				double hf = stm->hanning_full[i];

				rms_wh += hf * val * val;		
				mean_wh += hf * val;
			}
			rms_wh = sqrt(rms_wh);

			// rev_win_han
			double ar[3];

			int i_max = rev_win_han_scan(ph->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);

			(*cui)->rms = rms_wh;
			(*cui)->rms_1h = ar[2];
			(*cui)->mid = mean_wh;
			
			(*cui)++;
		}
	}
}

void calc_ui_diff_stream(int stm_idx, uint8_t phases_mask, calc_ui **cui_diff)
{
	calc_stream *stm = stream[stm_idx];
	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];

			for (int pp = p + 1; pp < PHASES_IN_STREAM; pp++) {
				if (phases_mask & (0x1<<pp)) {
					phase *pph = &stm->phases[pp];

					double rms_wh = 0.0;

					for (int i = 0; i < stm->v_size; i++) {
						double val = ph->values[i] - pph->values[i];
						double hf = stm->hanning_full[i];
						rms_wh += hf * val * val;		
					}
					rms_wh = sqrt(rms_wh);

					(*cui_diff)->rms = rms_wh;
					
					(*cui_diff)++;
				}
			}
		}
	}
}
