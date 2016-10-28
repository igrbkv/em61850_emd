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

int calc_p_stream(int stm_idx, uint8_t phases_mask, uint8_t reference, calc_p *cp);

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

int calc_p_stream(int stm_idx, uint8_t phases_mask, uint8_t reference, calc_p *cp)
{
	int count = 0;
	calc_p *_cp = cp;
	calc_stream *stm = stream[stm_idx];
	phase *ph_ref = &stm->phases[reference];
	int beg_idx = 3, 
		end_idx = stm->counts/2 - 1;

	int i_max = beg_idx;
	double temp = ph_ref->ampl_spectre[i_max];

	for(int i = beg_idx + 1; i < end_idx; i++) {
		if (ph_ref->ampl_spectre[i] > temp) {
			temp = ph_ref->ampl_spectre[i];
			i_max = i;
		}
	}

	for (int p = 0; p < PHASES_IN_STREAM/2; p++) {
		if ((phases_mask & (0x1<<p)) && (phases_mask & (0x1<<(p+PHASES_IN_STREAM/2)))) {
			phase *ph_i = &stm->phases[p];
			phase *ph_u = &stm->phases[p+PHASES_IN_STREAM/2];

			double rms_i = 0.0,
				rms_u = 0.0,
				p = 0.0,
				q = 0.0,
				s = 0.0,
				p_1h = 0.0,
				q_1h = 0.0,
				s_1h = 0.0,
				u_1h = 0.0,
				i_1h = 0.0,
				cos_phi = 0.0,
				sin_phi = 0.0,
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
			s = rms_u*rms_i;
			q = sqrt(s*s - p*p);

			// rev_win_han
			double ar_u[3], ar_i[3];

			rev_win_han_scan(ph_u->ampl_spectre, i_max -1, i_max + 1, ar_u, stm->counts, t_samp);
			rev_win_han_scan(ph_i->ampl_spectre, i_max -1, i_max + 1, ar_i, stm->counts, t_samp);

			p_1h = ph_u->data_complex_out[i_max*2]*
				ph_i->data_complex_out[i_max*2] +
				ph_u->data_complex_out[i_max*2+1]*
				ph_i->data_complex_out[i_max*2+1];
			
			q_1h = -(ph_u->data_complex_out[i_max*2]*
				ph_i->data_complex_out[i_max*2+1] -
				ph_u->data_complex_out[i_max*2+1]*
				ph_i->data_complex_out[i_max*2]);
			
			u_1h = sqrt(pow(ph_u->data_complex_out[i_max*2], 2) +
				pow(ph_u->data_complex_out[i_max*2+1], 2));
			i_1h = sqrt(pow(ph_i->data_complex_out[i_max*2], 2) +
				pow(ph_i->data_complex_out[i_max*2+1], 2));

			cos_phi = p_1h/(u_1h*i_1h);
			sin_phi = q_1h/(u_1h*i_1h);
			p_1h = 2*p_1h*ar_u[1]*ar_i[1];
			q_1h = 2*q_1h*ar_u[1]*ar_i[1];
			s_1h = 2*u_1h*i_1h*ar_u[1]*ar_i[1];

			_cp->p = p;
			_cp->q = q;
			_cp->s = s;
			_cp->rms_u = rms_u;
			_cp->rms_i = rms_i;
			_cp->p_1h = p_1h;
			_cp->q_1h = q_1h;
			_cp->s_1h = s_1h;
			_cp->cos_phi = cos_phi;
			_cp->sin_phi = sin_phi;
			
			_cp++;
			count++;
		}
	}
	return count;
}
