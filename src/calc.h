#ifndef CALC_H_
#define CALC_H_

#include <stdint.h>

typedef struct phase {
	double values[RATE_MAX];
	double data_complex_out[RATE_MAX*2];
	double data_wh[RATE_MAX];
	double data_wh_wz[RATE_MAX*2];
	double ampl_spectre[RATE_MAX];
} phase;

typedef struct calc_stream {
	int counts;
	int v_size;
	double hanning_full[RATE_MAX];
	phase phases[PHASES_IN_STREAM];
} calc_stream;

extern calc_stream *stream[2];

int calc_init();
int calc_close();

void set_stream_values(int stm_idx, uint8_t phases_mask, sv_data *svd, int svd_size);
void prepare_phases(int stm_idx, uint8_t phases_mask);

void alloc_fft(int num_samples);
void free_fft();


// На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера
extern unsigned int EQ_TRAILS;

extern double Kf;
#endif
