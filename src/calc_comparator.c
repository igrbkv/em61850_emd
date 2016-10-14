#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"
#include "compute.h"

#include "debug.h"
#ifdef DEBUG
#include <stdio.h>
#endif


static void do_calculations(double *data, int len, struct calc_general **calc_res, int *calc_res_size); 

// @param idx1: idx of signal of stream 1
// @param idx2: idx of signal of stream 1 or 2
// @param  ts: time stamp
// @param  c1: values of signal 1
// @param  c2: values of signal 2
int make_calc(uint8_t strm1, uint8_t strm2, struct timeval *ts, struct calc_general **c1, int *c1_size, struct calc_general **c2, int *c2_size)
{
	sv_data *s1, *s2, *s = NULL;
	int s1_size = 0, s2_size = 0, s_size;
	int stream2 = (idx1 >= STREAM2_START_IDX) || (idx2 >= STREAM2_START_IDX);
	int stream1 = (idx1 < STREAM2_START_IDX) || (idx2 < STREAM2_START_IDX);

	*c1_size = *c2_size = 0;

	sv_get_ready(ts, stream1? &s1: NULL, stream1? &s1_size: NULL, stream2? &s2: NULL, stream2? &s2_size: NULL);

	if (s1_size == 0 && s2_size == 0)
		return;

	double *values = calloc(s1_size > s2_size? s1_size: s2_size, sizeof(double));
	
	if (idx1 < STREAM2_START_IDX && s1_size) {
		s = s1;
		s_size = s1_size;
	} else if (idx1 >= STREAM2_START_IDX && s2_size) {
		idx1 %= STREAM2_START_IDX;
		s = s2;
		s_size = s2_size;
	}
	if (s) {
		double sf = scaleFactor(idx1);
		for (int i = 0; i < s_size; i++) {
			int v = s[i].values[idx1*2];
			values[i] = sf*(double)v;
		}
		do_calculations(values, s_size, c1, c1_size);
	}

	if (idx2 < STREAM2_START_IDX && s1_size) {
		s = s1;
		s_size = s1_size;
	} else if (idx2 >= STREAM2_START_IDX && s2_size) {
		idx2 %= STREAM2_START_IDX;
		s = s2;
		s_size = s2_size;
	}
	if (s) {
		double sf = scaleFactor(idx1);
		for (int i = 0; i < s_size; i++) {
			int v = s[i].values[idx2*2];
			values[i] = sf*(double)v;
		}
		do_calculations(values, s_size, c2, c2_size);
	}

	if (dump) {
		// TODO add harmonics
		char buf[128];
		char *sig_names[] = {
			"Stream1_Ia",
			"Stream1_Ib",
			"Stream1_Ic",
			"Stream1_In",
			"Stream1_Ua",
			"Stream1_Ub",
			"Stream1_Uc",
			"Stream1_Un",
			"Stream2_Ia",
			"Stream2_Ib",
			"Stream2_Ic",
			"Stream2_In",
			"Stream2_Ua",
			"Stream2_Ub",
			"Stream2_Uc",
			"Stream2_Un",
		};
		snprintf(buf, sizeof(buf), "/tmp/dump_%d_%d", sig_names[idx1], sig_names[idx2]);
		FILE *fp = fopen(buf, "a");
		fprintf(fp, "************************\n");
		fprintf(fp, "time stamp:%lu.%06lu\n", ts->tv_sec, ts->tv_usec);
		fprintf(fp, "Signal: %s =======\n", sig_names[idx1]);
		if (*c1_size) {
			fprintf(fp, "rms:%.8f dc:%.8f f_1h:%.8f rms_1h:%.8f phi:%.8f deg thd:%.8f\n", 
				(*c1)->rms,
				(*c1)->dc,
				(*c1)->f_1h,
				(*c1)->rms_1h,
				(*c1)->phi*180./M_PI,
				(*c1)->thd);
		}
		fprintf(fp, "Signal: %s =======\n", sig_names[idx2]);
		if (*c2_size) {
			fprintf(fp, "rms:%.8f dc:%.8f f_1h:%.8f rms_1h:%.8f phi:%.8f deg thd:%.8f\n", 
				(*c2)->rms,
				(*c2)->dc,
				(*c2)->f_1h,
				(*c2)->rms_1h,
				(*c2)->phi*180./M_PI,
				(*c2)->thd);
		}

		if (s1_size == s2_size)
			for (int i = 0; i < s1_size; i++)
				fprintf(fp, "%8d,%8d,%8d\n", i, s1[i].values[idx1 * 2],  s2[i].values[idx2 % STREAM2_START_IDX * 2]);
		else {
			if (s1_size) {
				if (!stream2)
					for (int i = 0; i < s1_size; i++)
						fprintf(fp, "%8d,%8d,%8d\n", i, s1[i].values[idx1 * 2],  s1[i].values[idx2 * 2]);
				else {
					fprintf(fp, "111111111111111111111111\n");
					for (int i = 0; i < s1_size; i++)
						fprintf(fp, "%8d: %8d\n", i, s1[i].values[idx1 * 2]);
				}
			}

			if (s2_size) {
				if (!stream1)
					for (int i = 0; i < s2_size; i++)
						fprintf(fp, "%8d,%8d,%8d\n", i, s2[i].values[idx1 % STREAM2_START_IDX * 2],  s2[i].values[idx2 % STREAM2_START_IDX * 2]);
				else {
					fprintf(fp, "222222222222222222222222\n");
					for (int i = 0; i < s2_size; i++)
						fprintf(fp, "%8d: %8d\n", i, s2[i].values[idx2 % STREAM2_START_IDX * 2]);
				}
			}
		}

		fclose(fp);
	}

	if (s1_size)
		free(s1);
	if (s2_size)
		free(s2);
	free(values);
}

double scaleFactor(int idx)
{
	return idx%STREAM2_START_IDX < PHASES_NUM? 0.001: 0.01;
}

void do_calculations(double *data, int len, struct calc_general **calc_res, int *calc_size) 
{
	int v_size = len - EQ_TRAILS;
	double *data_complex_out;
	// FIXME if len = 1 sec
	double t_samp = 1./len;
	
	data_complex_out = calloc(len*2, sizeof(double));
	double *hanning_full = calloc(v_size, sizeof(double));
	double *data_wh = calloc(v_size, sizeof(double));
	double *tmp_data = calloc(len*2, sizeof(double));
	// synt hanning
	for (int i = 0, k = -(v_size -1); i < v_size; i++, k+=2) {
		hanning_full[i] = (1. + cos(M_PI/v_size*k))/v_size;
	}

	// calc RMS
	double rect_mean_h = 0.0,
		   rms_wh = 0.0,
		   mean_wh = 0.0,
		   ampl = data[0];

	double *data_wh_wz = tmp_data;	   
	memset(data_wh_wz, 0, len*2 * sizeof(double));

	for (int i = 0; i < v_size; i++) {
		rect_mean_h += fabs(data[ i ] * hanning_full[ i ]);
		rms_wh += hanning_full[ i ] * data[ i ] * data[ i ];		
		mean_wh += hanning_full[ i ] * data[ i ];
		data_wh[i] = data[ i ] * hanning_full[ i ];
		if (data[i] > ampl)
			ampl = data[i];

		data_wh_wz[i*2] = data_wh[i] ;
	}
	rms_wh = sqrt(rms_wh);

	// преобр Фурье
	general_transform(data_wh_wz, data_complex_out, len, 1);

	// ampl spectre
	double *ampl_spectre = tmp_data;
	memset(ampl_spectre, 0, len*2*sizeof(double));
	for (int i = 0; i < 2*len; i+= 2)
		ampl_spectre[i/2] = sqrt(pow(data_complex_out[i], 2) + pow(data_complex_out[i + 1], 2));

	// rev_win_han
	double ar[3];

	int i_max = rev_win_han_scan(ampl_spectre, 3, len / 2 - 1, ar, len, t_samp);

	double abs_phi = calc_abs_phi(data_complex_out, t_samp, i_max, len);

	ar[ 0 ] *= Kf;
	
	const int max_harm_calc = round(1.0 / ( 2 * t_samp * ar[ 0 ] ) - 0.5);
	const int max_harm = (max_harm_calc < harmonics_count ? max_harm_calc : harmonics_count);

#if 0 
	*calc_size = sizeof(struct calc_results) + sizeof(struct harmonic)*(max_harm > 2? max_harm - 2: 0);
#endif
	*calc_size = sizeof(struct calc_general);

	*calc_res = malloc(*calc_size);

	double thd = 0.;

	for	(int i = 2; i < max_harm; i++) {
		int idx = len * t_samp * ar[0] * i + 0.5;
		
		double ar_cur[3];		
		i_max = rev_win_han_scan(ampl_spectre, idx - 1, idx + 1, ar_cur, len, t_samp);
#if 0
		(*calc_res)->h[i-2].f = ar_cur[0];
		(*calc_res)->h[i-2].k = ar_cur[1];
		(*calc_res)->h[i-2].ampl = ar_cur[2];
#endif
		thd += pow(ar_cur[2], 2);
	}
	thd = sqrt(thd) / ar[2];

#if 0
	(*calc_res)->harmonics_num = max_harm < 2? 0: max_harm - 2;
#endif

	(*calc_res)->rms = rms_wh;
	(*calc_res)->dc = mean_wh;
	(*calc_res)->f_1h = ar[0];
	(*calc_res)->rms_1h = ar[2];
	(*calc_res)->phi = abs_phi;
	(*calc_res)->thd = thd;

	free(data_complex_out);
	free(hanning_full);
	free(data_wh);
	free(tmp_data);
}


