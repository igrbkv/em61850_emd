#ifndef CALC_DATA_H_
#define CALC_DATA_H_

struct timeval;
struct calc_data;

void make_calc_data(int idx1, int idx2, int scale, int begin, int length, int counts_limit, struct timeval *ts, struct calc_data **cd, int *cd_size);

#endif
