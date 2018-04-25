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

static void calc_harm_stream(int stm_idx, uint8_t phases_mask, calc_harmonics *charms, int *charms_sz);

// @param req: req => resp
// @param cmpr: calc_comparator's ptr
// @return 0  - Ok
//         <0 - error
int make_calc_harm(calc_req *req, calc_harmonics *charms, int *charms_sz)
{
	sv_data *svd[2];
	int svd_size[2] = {0, 0};
	*charms_sz = 0;

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
			calc_harm_stream(i, req->stream[i], charms, charms_sz);
		}

	return 0;
}

void calc_harm_stream(int stm_idx, uint8_t phases_mask, calc_harmonics *charms, int *charms_sz)
{
	int offset = 0;
	char *buf = &((char *)charms)[*charms_sz];
	calc_stream *stm = stream[stm_idx];
	double t_samp = 1./stm->counts;

	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];
			calc_harmonics *ch = (struct calc_harmonics *)&buf[offset];

			// rev_win_han
			double ar[3];

			rev_win_han_scan(ph->ampl_spectre, 3, stm->counts / 2 - 1, ar, stm->counts, t_samp);

			ar[ 0 ] *= Kf;

			int max_harm_calc = round(1.0 / ( 2 * t_samp * ar[ 0 ] ) - 0.5);
			int max_harm = (max_harm_calc < harmonics_count ? max_harm_calc : harmonics_count);
			if (max_harm < 0)
				max_harm = 0;

			ch->harmonics_num = max_harm;
			ch->f_1h = ar[0];
			offset += sizeof(struct calc_harmonics);
			if (max_harm) {
				ch->h[0].ampl = ar[2];
				offset += sizeof(struct calc_harmonic);

				for	(int i = 1; i < max_harm; i++) {
					int idx = stm->counts * t_samp * ar[0] * (i+1) + 0.5;
					
					double ar_cur[3];		
					rev_win_han_scan(ph->ampl_spectre, idx - 1, idx + 1, ar_cur, stm->counts, t_samp);
					
					ch->h[i].ampl = ar_cur[2];
					
					offset += sizeof(struct calc_harmonic);
				}
			}
		}
	}
	(*charms_sz) = offset;
}
