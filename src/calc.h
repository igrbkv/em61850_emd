#ifndef CALC_H_
#define CALC_H_

#include <stdint.h>
#include "sv_read.h"
#include "proto.h"

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
extern int calc_second_divider;

int calc_init();
int calc_close();

int make_comparator_calc(calc_req *req, calc_comparator *cmpr, int *cmpr_sz);
int make_calc_ui(calc_multimeter_req *req, calc_ui *cui,int *cui_sz, calc_ui_diff *cui_diff, int *cui_diff_sz);
int make_calc_p(calc_multimeter_req *req, calc_p *cp);
int make_calc_data(calc_data_req *req, calc_data **data, int *data_size);
int make_calc_a(calc_multimeter_req *cmr, calc_a *ca);
int make_calc_harm(calc_req *req, calc_harmonics *charmonics, int *charmonics_sz);

void set_stream_values(int stm_idx, uint8_t phases_mask, sv_data *svd, int svd_size);
void prepare_phases(int stm_idx, uint8_t phases_mask);

double scale_factor(int idx);

void alloc_fft(int num_samples);
void free_fft();


// На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера
extern unsigned int EQ_TRAILS;

extern int harmonics_count;
extern double Kf;
#endif
