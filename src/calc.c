#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"

#include "debug.h"
#ifdef DEBUG
#include <stdio.h>
#endif


const unsigned int EQ_TRAILS = 0; // На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера

double Kf = 1.0;
static int harmonics_count = 6;
//static int subharmonics_count = 6;

static void do_calculations(double *data, int len, struct calc_results **calc_res, int *calc_res_size); 
static double calc_abs_phi(const double *data_complex, double t_samp, int i, int sb);
static unsigned int rev_win_han_scan(double s_buf[], unsigned int min_index, unsigned int max_index, double ar[], unsigned int sb, const double t_samp); 
static void general_transform(double inp_v[], double out_v[], unsigned int num_samples, int dir); 
static void dfour1(double data[], unsigned int nn2, int isign);

// @param idx1: idx of signal of stream 1
// @param idx2: idx of signal of stream 1 or 2
// @param  ts: time stamp
// @param  c1: values of signal 1
// @param  c2: values of signal 2
void make_calc(int idx1, int idx2, struct timeval *ts, struct calc_results **c1, int *c1_size, struct calc_results **c2, int *c2_size)
{
	sv_data *s1, *s2;
	int s1_size = 0, s2_size = 0;
	int stream2 = (idx1 >= STREAM2_START_IDX) || (idx2 >= STREAM2_START_IDX);
	int stream1 = (idx1 < STREAM2_START_IDX) || (idx2 < STREAM2_START_IDX);

	*c1_size = *c2_size = 0;

	sv_get_ready(ts, stream1? &s1: NULL, stream1? &s1_size: NULL, stream2? &s2: NULL, stream2? &s2_size: NULL);

	if (s1_size == 0 && s2_size == 0)
		return;

	double *values = calloc(s1_size > s2_size? s1_size: s2_size, sizeof(double));

	if (s1_size) {
		for (int i = 0; i < s1_size; i++) {
			int v = s1[i].values[idx1*2];
			values[i] = (double)v;
		}

		do_calculations(values, s1_size, c1, c1_size);

		if (!stream2) {
			for (int i = 0; i < s1_size; i++) {
				int v = s1[i].values[idx2*2];
				values[i] = (double)v;
			}

			do_calculations(values, s1_size, c2, c2_size);
		}
	}
	
	if (s2_size) {
		int idx;
		if (!stream1) {
			idx = idx1 % STREAM2_START_IDX;
			for (int i = 0; i < s2_size; i++) {
				int v = s2[i].values[idx*2];
				values[i] = (double)v;
			}

			do_calculations(values, s2_size, c1, c1_size);
		}

		idx = idx2 % STREAM2_START_IDX;
		for (int i = 0; i < s2_size; i++) {
			int v = s2[i].values[idx*2];
			values[i] = (double)v;
		}

		do_calculations(values, s2_size, c2, c2_size);
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

void do_calculations(double *data, int len, struct calc_results **calc_res, int *calc_size) 
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

	*calc_size = sizeof(struct calc_results) + sizeof(struct harmonic)*(max_harm > 2? max_harm - 2: 0);
	*calc_res = malloc(*calc_size);

	double thd = 0.;

	for	(int i = 2; i < max_harm; i++) {
		int idx = len * t_samp * ar[0] * i + 0.5;
		
		double ar_cur[3];		
		i_max = rev_win_han_scan(ampl_spectre, idx - 1, idx + 1, ar_cur, len, t_samp);

		(*calc_res)->h[i-2].f = ar_cur[0];
		(*calc_res)->h[i-2].k = ar_cur[1];
		(*calc_res)->h[i-2].ampl = ar_cur[2];
		thd += pow(ar_cur[2], 2);
	}
	thd = sqrt(thd) / ar[2];

	(*calc_res)->harmonics_num = max_harm < 2? 0: max_harm - 2;
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

double calc_abs_phi(const double *data_complex, double t_samp, int i, int sb) 
{
	double ar[3];

	int				sigd;
	double			x1, x2;
	double			temp;
	double			alp;

	temp = sqrt(pow(data_complex[(i - 1) * 2], 2) + 
		pow(data_complex[(i - 1) * 2 + 1], 2) );
	x1 = sqrt(pow(data_complex[i * 2], 2) + 
		pow(data_complex[i * 2 + 1], 2));
	x2 = sqrt(pow(data_complex[(i + 1) * 2], 2) + pow(data_complex[(i + 1) * 2 + 1], 2) );
			

	if (x2 < temp) {
		sigd = -1;
		x2 = temp;
	} else 
		sigd = 1;

	alp = (2 * x2 - x1) / (x2 + x1);
	if (abs(alp) > 0.000002)
		ar[1] = M_PI * alp * (1 - pow(alp,2)) / (sin(M_PI * alp)); // коэффициент поправки 
	else
		ar[1] = 1;

	ar[0] = (i + sigd * alp) / (sb * t_samp); // частота
	ar[2] = ar[1] * x1 * M_SQRT2; // амплитуда
		
	//Расчет сдвига фаз относительно начала измерений
	// TODO: Проверить работу по квадрантам

	double cos_phi = (data_complex[i*2] / x1);
	double sin_phi = (data_complex[i*2 + 1] / x1);
	double at2 = 0.0;

	if (cos_phi > 0.0 && sin_phi > 0.0) {
		if (fabs(cos_phi) < 0.707)
			at2 = acos(cos_phi);
		else
			at2 = asin(sin_phi);
	} else if (cos_phi > 0.0 && sin_phi < 0.0) {
		if (fabs(cos_phi) < 0.707)
			at2 = -acos(cos_phi);
		else
			at2 = asin(sin_phi);
	} else if (cos_phi < 0.0 && sin_phi < 0.0) {
		if (fabs(cos_phi) < 0.707)
			at2 = -acos(cos_phi);
		else
			at2 = -M_PI - asin(sin_phi);
	} else if (cos_phi < 0.0 && sin_phi > 0.0) {
		if (fabs(cos_phi) < 0.707)
			at2 = acos(cos_phi);
		else
			at2 = M_PI - asin(sin_phi);
	}

	double absPhi = at2 - sigd*alp*M_PI*(1-1/sb);

	if (absPhi > M_PI) {
		absPhi -= 2*M_PI;
	} else if (absPhi < -M_PI) {
		absPhi += 2*M_PI;
	}

	return absPhi;
}

void general_transform(double inp_v[], double out_v[], unsigned int num_samples, int dir) 
{
	unsigned int k, p;
	unsigned int esize;
	unsigned int ne, nn;
	double pp, arg, pn, bk, bk1;

	double* b;
	double* bb;
	double* vvv;

    pp = log(4 * num_samples - 1.) / log(2.);
    p = floor(pp);
    esize = 2 << (p-1);

    ne = esize << 1;

	b = calloc(ne, sizeof(double)); 
	bb = calloc(ne, sizeof(double)); 
	vvv = calloc(ne, sizeof(double)); 
	memset(b, 0, sizeof(double)*ne);
	memset(vvv, 0, sizeof(double)*ne);

    nn = num_samples << 1;
	pn = M_PI / num_samples;

	memcpy(vvv, inp_v, sizeof(double)*nn);

    for(k = 0;k < nn; k += 2) {
        arg =  k / 2.0;
		arg *= arg;
		arg *=pn;
        bk = cos(arg);
        bk1 = sin(arg);
        *(b + k) = bk;
        *(b + k + 1) = bk1;
        pp = vvv[k] * bk + vvv[k + 1] * bk1;
        vvv[k + 1] = vvv[k + 1] * bk - vvv[k] * bk1;
        vvv[k] = pp;
	}

    for(k = 2; k < nn; k += 2) {
		p = 2 * esize - k;
        *(b + p + 1) = *(b + k + 1);
        *(b + p) = *(b + k);
	}

	memcpy(bb, b, sizeof(double)*ne);
	
    dfour1(vvv-1, esize, 1);
    dfour1(b-1, esize, 1);

	for(k = 0; k < ne; k += 2) {
        pp = vvv[k] * b[k] - vvv[k + 1] * b[k + 1];
        vvv[k + 1] = vvv[k + 1] * b[k] + vvv[k] * b[k + 1];
        vvv[k] = pp;
	}

    dfour1(vvv-1, esize, -1);

	for(k = 0; k < 2*num_samples; k += 2) {
        out_v[k]  = (vvv[k] * bb[k] + vvv[k+1] * bb[k+1]);
        out_v[k+1] = (vvv[k+1] * bb[k] - vvv[k] * bb[k+1]);
	}

	free(b);
	free(bb);
	free(vvv);
}

void dfour1(double data[], unsigned int nn2, int isign) {
	//преобразование Фурье двойной точности, взято из Numerical Recipes
	//подлежит проверке точность алгоритма
	//? с точностью синтеза поворачивающих множителей
	unsigned int n,mmax,m,j,istep,i;
	double wtemp,wr,wpr,wpi,wi,theta;
	double tempr,tempi;
	n=nn2 << 1;
	j=1;

	for (i=1;i<n;i+=2) {
		if (j > i) {
			double tmp = data[j];
			data[j] = data[i];
			data[i] = tmp;

			tmp = data[j + 1];
			data[j + 1] = data[i + 1];
			data[i + 1] = tmp;
		}
		m=n >> 1;
		while (m >= 2 && j > m) {
			j -= m;
			m >>= 1;
		}
		j += m;
	}
	mmax=2;

	while (n > mmax) {
		istep=mmax << 1;
		theta=isign*(6.28318530717959/mmax);
		wtemp=sin(0.5*theta);
		wpr = -2.0*wtemp*wtemp;
		wpi=sin(theta);
		wr=1.0;
		wi=0.0;
		for (m=1;m<mmax;m+=2) {
			for (i=m;i<=n;i+=istep) {
				j=i+mmax;
				tempr=wr*data[j]-wi*data[j+1];
				tempi=wr*data[j+1]+wi*data[j];
				data[j]=data[i]-tempr;
				data[j+1]=data[i+1]-tempi;
				data[i] += tempr;
				data[i+1] += tempi;
			}
			wr=(wtemp=wr)*wpr-wi*wpi+wr;
			wi=wi*wpr+wtemp*wpi+wi;
		}
		mmax=istep;
	}

	for (i=3;i<nn2;i+=2)
	{
		tempr = data[i];
		tempi = data[i + 1];
		n = 2*(nn2+1)-i;
		data[i] = data[n];
		data[i + 1] = data[n + 1];
		data[n] = tempr;
		data[n + 1] = tempi;
	} 
	if (isign<0)
	{
		for (i=1;i<=2*nn2;i++)
		{
			data[i] = data[i] / nn2;
		}
	}

}

unsigned int rev_win_han_scan(double s_buf[], unsigned int min_index, unsigned int max_index, double ar[], unsigned int sb, const double t_samp) 
{
	unsigned int	i, i_max;
	int				sigd;
	double			x1, x2;
	double			temp, temp2;
	double			b;

	i_max = min_index;
	temp = 0;
	for(i = min_index; i < max_index; i++) {
		if (s_buf[i] > temp) {
			i_max = i;
			temp = s_buf[i];
		}
	}

	x1 = s_buf[i_max];

	if ((i_max - 1) < min_index)
		temp = s_buf[i_max + 1];
	else
		temp = s_buf[i_max - 1];

	if ((i_max + 1) > max_index)
		temp2 = s_buf[i_max - 1];
	else
		temp2 = s_buf[i_max + 1];

	sigd = 1;
	x2 = temp2;
	if (temp2 < temp) {
		sigd = -1;
		x2 = temp;
	}

	b = (2 * x2 - x1) / (x2 + x1);
	if (abs(b) > 0.000002)
		ar[1] = M_PI * b * (1 - b*b) / (sin(M_PI * b)); // коэффициент поправки 
	else
		ar[1] = 1;

	ar[0] = (i_max + sigd * b) / (sb * t_samp); // частота
	ar[2] = ar[1] * x1 * 1.414213562373095; // амплитуда

	
	return i_max;
}
