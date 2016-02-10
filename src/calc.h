#ifndef CALC_H_
#define CALC_H_

void do_calculations(double *data, int len,  double *data_comlex); 

struct u_ab;
struct ua_ua;
int make_u_ab(struct u_ab **uab);
int make_ua_ua(struct ua_ua **uaua);

#endif
