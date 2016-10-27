#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"
#include "calc_math.h"

#include "debug.h"

static calc_multimeter_req last_req;

int calc_p_stream(int stm_idx, uint8_t phases_mask, uint8_t reference_mask, calc_p *cp);

// @param req: req => resp
// @param cpq: calc_pq's ptr
// @return n - num of filled structs 
//         <0 - error
int make_calc_p(calc_multimeter_req *cmr, calc_p *cp)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};

	calc_req *req = &cmr->req;

	if (req->stream[0] == 0 && req->stream[1] == 0)
		return 0;

	sv_get_ready(&req->time_stamp, req->stream[0]? &svd[0]: NULL, req->stream[0]? &svd_size[0]: NULL, req->stream[1]? &svd[1]: NULL, req->stream[1]? &svd_size[1]: NULL);

	if (memcmp(&last_req, cmr, sizeof(calc_multimeter_req)) == 0)
		return -ERR_RETRY;

	last_req = *cmr;

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
			ps += calc_p_stream(i, req->stream[i], cmr->reference[i], &cp[ps]);
		}

	return ps;
}

int calc_p_stream(int stm_idx, uint8_t phases_mask, uint8_t reference_mask, calc_p *cp)
{
	int count = 0;
	calc_p *_cp = cp;
	calc_stream *stm = stream[stm_idx];
	for (int p = 0; p < PHASES_IN_STREAM/2; p++) {
		if ((phases_mask & (0x1<<p)) && (phases_mask & (0x1<<(p+PHASES_IN_STREAM/2)))) {
			phase *ph_i = &stm->phases[p];
			phase *ph_u = &stm->phases[p+PHASES_IN_STREAM/2];

			double rms_i = 0.0,
				rms_u = 0.0,
				p = 0.0,
				p_1h = 0.0,
				rms_1h_i = 0.0,
				rms_1h_u = 0.0,
				cos_phi = 0.0,
				t_samp = 1./stm->counts;

			for (int i = 0; i < stm->v_size; i++) {
				double val_i = ph_i->values[i];
				double val_u = ph_u->values[i];
				double hf = stm->hanning_full[i];

				rms_i += hf * val_i * val_i;		
				rms_u += hf * val_u * val_u;
				p += hf * val_i * val_u;
			}
			rms_i = sqrt(rms_i);
			rms_u = sqrt(rms_u);

			// rev_win_han
			double ar[3];

			int i_max = rev_win_han_scan(ph_u->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);
			rms_1h_u = ar[2];
			rev_win_han_scan(ph_i->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);
			rms_1h_i = ar[2];

			_cp->rms_u = rms_u;
			_cp->rms_i = rms_i;
			_cp->p = p;
			_cp->p_1h = p_1h;
			_cp->cos_phi = cos_phi;
			
			_cp++;
			count++;
		}
	}
	return count;
}
