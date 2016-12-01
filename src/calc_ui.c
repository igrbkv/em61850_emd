#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"
#include "calc_math.h"
#include "log.h"

#include "debug.h"

#define U_MASK 0xf0
#define I_MASK 0x0f

static calc_req last_req;

static void calc_ui_stream(int stm_idx, uint8_t phases_mask, calc_ui *cui, int *cui_sz);
static void calc_ui_diff_stream(int stm_idx, uint8_t phases_mask, calc_ui_diff *cui_diff, int *cui_diff_sz);

// @param req: req => resp
// @param cmpr: calc_comparator's ptr
// @return 0  - Ok
//         <0 - error
int make_calc_ui(calc_multimeter_req *cmr, calc_ui *cui, int *cui_sz, calc_ui_diff *cui_diff, int *cui_diff_sz)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};
	*cui_sz = 0;
	*cui_diff_sz = 0;

	// Опорный сигнал не используется
	calc_req * req = &cmr->req;

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


	for (int i = 0; i < 2; i++)
		if (svd_size[i]) {
			set_stream_values(i, req->stream[i], svd[i], svd_size[i]);
			prepare_phases(i, req->stream[i]);
			calc_ui_stream(i, req->stream[i], cui, cui_sz);
		}


	// FIXME потоки могут быть разной частоты дискретизации
	// поэтому считаются отдельно
	
	for (int i = 0; i < 2; i++)
		if (svd_size[i])
			calc_ui_diff_stream(i, req->stream[i], cui_diff, cui_diff_sz);

	return 0;
}

void calc_ui_stream(int stm_idx, uint8_t phases_mask, calc_ui *cui, int *cui_sz)
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

			rev_win_han_scan(ph->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);

			cui[*cui_sz].rms = rms_wh;
			cui[*cui_sz].rms_1h = ar[2];
			cui[*cui_sz].mid = mean_wh;
			
			(*cui_sz)++;
		}
	}
}

void calc_ui_diff_stream(int stm_idx, uint8_t phases_mask, calc_ui_diff *cui_diff, int *cui_diff_sz)
{
	// только напряжение
	phases_mask &= U_MASK;
	calc_stream *stm = stream[stm_idx];
	for (int p = PHASES_IN_STREAM/2; p < PHASES_IN_STREAM; p++) {
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

					cui_diff[*cui_diff_sz].value = rms_wh;
					
					(*cui_diff_sz)++;
				}
			}
		}
	}
}
