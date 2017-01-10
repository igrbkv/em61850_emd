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

// делитель секундного отрезка данных
int calc_second_divider = 1;

unsigned int EQ_TRAILS = 0; // На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера

double Kf = 1.0;
int harmonics_count = 6;
//static int subharmonics_count = 6;

calc_stream *stream[2];

int calc_init()
{
	stream[0] = malloc(sizeof(calc_stream));
	memset(stream[0], 0, sizeof(calc_stream));
	stream[1] = malloc(sizeof(calc_stream));
	memset(stream[1], 0, sizeof(calc_stream));
	alloc_fft(RATE_MAX);
	return 0;
}

int calc_close()
{
	free(stream[0]);
	free(stream[1]);
	free_fft();
	return 0;
}

double scale_factor(int idx)
{
	return idx < PHASES_IN_STREAM/2? 0.001: 0.01;
}

void set_stream_values(int stm_idx, uint8_t phases_mask, sv_data *svd, int svd_size)
{
	calc_stream *stm = stream[stm_idx];
	int v_size_last = stm->v_size;

	stm->counts = svd_size/calc_second_divider;
	stm->v_size = stm->counts - EQ_TRAILS;

	memset(stm->phases, 0, sizeof(phase)*PHASES_IN_STREAM);

	// recalculate hanning_full
	if (v_size_last != stm->v_size)
		for (int i = 0, k = -(stm->v_size - 1); i < stm->v_size; i++, k+=2)
			stm->hanning_full[i] = (1. + cos(M_PI/stm->v_size*k))/stm->v_size;

	// set phases values
	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];
			memset(ph, 0, sizeof(phase));
			double sf = scale_factor(p);
			for (int i = 0; i < stm->counts; i++) {
				int v = svd[i].values[p*2];
				ph->values[i] = sf*(double)v;
			}
		}
	}
}

void prepare_phases(int stm_idx, uint8_t phases_mask)
{
	calc_stream *stm = stream[stm_idx];
	for (int p = 0; p < PHASES_IN_STREAM; p++) {
		if (phases_mask & (0x1<<p)) {
			phase *ph = &stm->phases[p];
			for (int i = 0; i < stm->v_size; i++) {
				ph->data_wh[i] = ph->values[i] * stm->hanning_full[i];
				ph->data_wh_wz[i*2] = ph->data_wh[i];
			}
			general_transform(ph->data_wh_wz, ph->data_complex_out, stm->counts, 1);
			for(int i = 0, j = 0; i < stm->counts; i++, j +=2)
				ph->ampl_spectre[i] = sqrt(pow(ph->data_complex_out[j], 2) + pow(ph->data_complex_out[j + 1], 2));
		}
	}	
}
