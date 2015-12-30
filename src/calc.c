#include <stdlib.h>
#include <string.h>
#include <math.h>

const unsigned int EQ_TRAILS = 0; // На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера

double Kf = 1.0;
static int harmonics_count = 6;
//static int subharmonics_count = 6;

static double calc_abs_phi(const double *data_complex, double t_samp, int i, int sb);
static unsigned int rev_win_han_scan(double s_buf[], unsigned int min_index, unsigned int max_index, double ar[], unsigned int sb, const double t_samp); 
static void general_transform(double inp_v[], double out_v[], unsigned int num_samples, int dir); 
static void dfour1(double data[], unsigned int nn2, int isign);

void do_calculations(double *data, int len,  double *data_complex) 
{
	int v_size = len - EQ_TRAILS;
	double *data_complex_out;
	// FIXME if len = 1 sec
	double t_samp = 1./len;
	
	if (data_complex)
		data_complex_out = data_complex;
	else
		data_complex_out = calloc(len*2, sizeof(double));
	double *hanning_full = calloc(v_size, sizeof(double));
	double *data_wh = calloc(v_size, sizeof(double));
	double *tmp_data = calloc(len*2, sizeof(double));
	// synt hanning
	for (int i = 0, k = -(v_size -1); i < v_size; i++, k+=2) {
		hanning_full[i] = (1. + cos(M_PI/v_size*k))/v_size;
	}

	// >>>>>масштабирование>>>>>>	
	//<<<<<масштабирование<<<<<<
	// mean_wh - Idc
	// rect_mean_h - Ivypr
	// Kf Ivypr/Irms
	// Kc Iampl/Irms
	// ampl - max of data


	// calc RMS
	double rect_mean_h = 0.0,
		   rms_wh = 0.0,
		   mean_wh = 0.0,
		   ampl = data[0];

	double *data_wh_wz = tmp_data;	   
	memset(data_wh_wz, 0, len*2 * sizeof(double));

	for (int i = 0, j = 0; i < v_size; i++, j += 2) {
		rect_mean_h += fabs(data[ i ] * hanning_full[ i ]);
		rms_wh += hanning_full[ i ] * data[ i ] * data[ i ];		
		mean_wh += hanning_full[ i ] * data[ i ];
		data_wh[i] = data[ i ] * hanning_full[ i ];
		if (data[i] > ampl)
			ampl = data[i];

		data_wh_wz[j] = data_wh[i] ;
	}
	rms_wh = sqrt(rms_wh);
	
	// TODO: пока считается только супремум
	
	//channelData->rms.addValue(rms_wh);
	//channelData->mean_wh.addValue(mean_wh);
	//channelData->rect_mean_h.addValue(rect_mean_h);
	//channelData->amplitude.addValue(ampl);
	//channelData->K_f.addValue(rms_wh / rect_mean_h);
	//channelData->K_s.addValue(ampl / rms_wh);
	//------------------------------------------------------------------------------------------

	// преобр Фурье
		
	// general_transform
	general_transform(data_wh_wz, data_complex_out, len, 1);

	// ampl spectre
	double *ampl_spectre = tmp_data;
	memset(ampl_spectre, 0, len*2*sizeof(double));
	for (int i = 0; i < 2*len; i+= 2)
		ampl_spectre[i] = sqrt(pow(data_complex_out[i], 2) + pow(data_complex_out[i + 1], 2));

	// rev_win_han
	double ar[3];

	int i_max = 0;
	i_max = rev_win_han_scan(ampl_spectre, 3, len / 2 - 1, ar, len, t_samp);

	//channelData->f_hard.addValue(ar[0]);
	//channelData->f_real.addValue(ar[0]);
	//ar[ 0 ] *= Kf;
	//channelData->firstHarmonic.addValue(ar[ 2 ]);
	//channelData->f_1.addValue(ar[ 0 ]);
	//channelData->absPhi.addValue(calc_abs_phi(data_complex, t_samp, aperture, i_max, data.size(),
	//channelData->dAbsdT.addValue(channelData->absPhi.getCurrent()/(2.0*M_PI*channelData->f_1.getCurrent()));

	// harmonics
	// подготовка значений индекса массива для расчета гармоник
	//const int max_harm = 15;

	const int max_harm_calc = round(1.0 / ( 2 * t_samp * ar[ 0 ] ) - 0.5);
	const int max_harm = (max_harm_calc < harmonics_count ? max_harm_calc : harmonics_count);

	//QVector<double> harmonics_freq;
	//QVector<double> harmonics_ampl;
	//QVector<double> harmonics_coef;
	//QVector<double> harmonics_absphi;
	//QVector<double> harmonics_absdt;
	
	// 0
	//harmonics_freq.push_back(0.0);	
	//harmonics_coef.push_back(1.0);
	//harmonics_ampl.push_back(mean_wh);
	//harmonics_absphi.push_back(0.0);
	//harmonics_absdt.push_back(0.0);
	// 1
	//harmonics_freq.push_back(ar[ 0 ]);
	//harmonics_coef.push_back(ar[ 1 ]);	
	//harmonics_ampl.push_back(ar[ 2 ]);
	//harmonics_absphi.push_back(channelData->absPhi.getCurrent());
	//harmonics_absdt.push_back(777/*channelData->dAbsdT.getCurrent()*/);

	for	(int i = 2; i < max_harm; i++) {
		int idx = len * t_samp * ar[0] * i + 0.5;
	
		double ar_cur[3];		
		i_max = rev_win_han_scan(ampl_spectre, idx - 1, idx + 1, ar_cur, len, t_samp);

		double absPhi = calc_abs_phi(data_complex_out, t_samp, i_max, len);

		//double absdT = channelData->absPhi.getCurrent()/(2.0*M_PI*ar_cur[0]);
		//harmonics_freq.push_back(ar_cur[ 0 ]);
		//harmonics_coef.push_back(ar_cur[ 1 ]);
		//harmonics_ampl.push_back(ar_cur[ 2 ]);
		//harmonics_absphi.push_back(absPhi);
		//harmonics_absdt.push_back(absdT);

		//todo angle between n-harm and 1st harm

		// Harmonics 4
		//channelData->harmonicsFreq = harmonics_freq;
		//channelData->harmonicsAmpl = harmonics_ampl;
		//channelData->harmonicsCoef = harmonics_coef;
		//channelData->harmonicsAbsPhi = harmonics_absphi;
		//channelData->harmonicsAbsdT = harmonics_absdt;
	}
	
#if 0
	// THD
	double THD_cur = 0.0;
	double *harmonics_k = tmp_data;
	memset(tmp_data, 0, len*2*sizeof(double));
	harmonics_k[0] = 100 * harmonics_ampl[ 0 ] / harmonics_ampl[ 1 ];
	harmonics_k[1] = 100;
	for(int i = 2; i < max_harm; i++) {
		harmonics_k[i] = 100 * harmonics_ampl[ i ] / harmonics_ampl[ 1 ];
		THD_cur += pow(harmonics_ampl[ i ], 2);
	}
	THD_cur = 100 * sqrt(THD_cur)/ harmonics_ampl[ 1 ];

	//channelData->harmonics_k = harmonics_k;
	//channelData->thd.addValue(THD_cur);
#endif

	free(tmp_data);
	if (!data_complex)
		free(data_complex_out);
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
	unsigned int Esize;
	unsigned int ne, nn;
	double pp, arg, pn, bk, bk1;

	double* b;
	double* bb;
	double* vvv;

    pp = log(4 * num_samples - 1.) / log(2.);
    p = floor(pp);
    Esize = 2 << (p-1);

    ne = Esize << 1;

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
		p = 2 * Esize - k;
        *(b + p + 1) = *(b + k + 1);
        *(b + p) = *(b + k);
	}

	memcpy(bb, b, sizeof(double)*ne);
	
    dfour1(vvv, Esize, 1);
    dfour1(b, Esize, 1);

	for(k = 0; k < ne; k += 2) {
        pp = vvv[k] * b[k] - vvv[k + 1] * b[k + 1];
        vvv[k + 1] = vvv[k + 1] * b[k] + vvv[k] * b[k + 1];
        vvv[k] = pp;
	}

    dfour1(vvv, Esize, -1);

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
	j=0;

	for (i=0;i<n;i+=2) {
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
		theta=isign*(M_PI*2./mmax);
		wtemp=sin(0.5*theta);
		wpr = -2.0*wtemp*wtemp;
		wpi=sin(theta);
		wr=1.0;
		wi=0.0;
		for (m=0;m<mmax;m+=2) {
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

	for (i=2;i<nn2;i+=2)
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
		for (i=0;i<=2*nn2;i++)
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
