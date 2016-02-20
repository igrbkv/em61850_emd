#ifndef CALC_H_
#define CALC_H_

void do_calculations(double *data, int len,  double *data_comlex); 

struct ui_ab;
struct ui_a_ui_a;
int make_ui_ab(struct ui_ab **ab, int u);
int make_ui_a_ui_a(struct ui_a_ui_a **aa, int u);

struct timeval;
struct calc_results;

void make_calc(int s1_idx, int s2_idx, struct timeval *ts, struct calc_results **c1, int *c1_size, struct calc_results **c2, int *c2_size);

#endif

