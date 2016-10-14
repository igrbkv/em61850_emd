#ifndef CALC_MATH_H_
#define CALC_MATH_H_

double calc_abs_phi(const double *data_complex, double t_samp, int i, int sb);
unsigned int rev_win_han_scan(double s_buf[], unsigned int min_index, unsigned int max_index, double ar[], unsigned int sb, const double t_samp); 
void general_transform(double inp_v[], double out_v[], unsigned int num_samples, int dir); 
void dfour1(double data[], unsigned int nn2, int isign);

#endif
