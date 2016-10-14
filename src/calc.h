#ifndef CALC_H_
#define CALC_H_

struct timeval;
struct calc_general;

int calc_init();
int calc_close();

void make_calc(int s1_idx, int s2_idx, struct timeval *ts, struct calc_general **c1, int *c1_size, struct calc_general **c2, int *c2_size);

double scaleFactor(int idx);

// На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера
extern unsigned int EQ_TRAILS;

extern double Kf;
#endif

