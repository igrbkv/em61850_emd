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

int calc_a_stream(int stm_idx, uint8_t phases_mask, calc_a *ca);

// @param req: req => resp
// @param ca: calc_a's ptr
// @return n - num of filled structs 
//         <0 - error
int make_calc_a(calc_multimeter_req *cmr, calc_a *ca)
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
			ps += calc_a_stream(i, req->stream[i], &ca[ps]);
		}

	return ps;
}

int calc_a_stream(int stm_idx, uint8_t phases_mask, calc_a *ca)
{
	int count = 0;
	calc_a *_ca = ca;
	calc_stream *stm = stream[stm_idx];

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];

			double t_samp = 1./stm->counts;
			// rev_win_han
			double ar[3];

			int i_max = rev_win_han_scan(ph->ampl_spectre, 3, stm->counts /2 - 1, ar, stm->counts, t_samp);

			double abs_phi = calc_abs_phi(ph->data_complex_out, t_samp, i_max, stm->counts);

			_ca->value = abs_phi;
			
			_ca++;
			count++;
		}
	}
	return count;
}
