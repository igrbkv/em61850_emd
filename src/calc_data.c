#include <stdlib.h>
#include "proto.h"
#include "sv_read.h"
#include "calc.h"

// @param idx1: idx of signal of stream 1
// @param idx2: idx of signal of stream 1 or 2
// @param scale: масштаб интервала
// @param begin: начало интервала
// @param length: длина интервала
// @param  ts: time stamp
// @param  cd: values of signals
// @param  cd_size: длина данных
void make_calc_data(int idx1, int idx2, int scale, int begin, int length, int counts_limit, struct timeval *ts, struct calc_data **cd, int *cd_size)
{
	sv_data *s1, *s2;
	int s1_size = 0, s2_size = 0;
	int stream2 = (idx1 >= STREAM2_START_IDX) || (idx2 >= STREAM2_START_IDX);
	int stream1 = (idx1 < STREAM2_START_IDX) || (idx2 < STREAM2_START_IDX);

	*cd_size = 0;

	sv_get_ready(ts, stream1? &s1: NULL, stream1? &s1_size: NULL, stream2? &s2: NULL, stream2? &s2_size: NULL);

	if (s1_size == 0 && s2_size == 0)
		return;

	*cd = malloc(sizeof(struct calc_data) + (s1_size + s2_size)*sizeof(float));

	if (s1_size) {
		float sf = scaleFactor(idx1);
		int first = s1_size/scale*begin;
		int step = 1;
		int counts = length*s1_size/scale;
		// шаг прореживания
		while (counts > counts_limit) {
			step++;
			counts /= step;
		}

		for (int i = 0; i < counts; i++) {
			int v = s1[first + i*step].values[idx1*2];
			cd->data[i] = sf*(float)v;
		}
		cd->size1 = counts;
	}
	
	if (s2_size) {
		float sf = scaleFactor(idx2);
		int first = s2_size/scale*begin;
		int step = 1;
		int counts = length*s2_size/scale;

		// шаг прореживания
		while (counts > counts_limit) {
			step++;
			counts /= step;
		}

		for (int i = 0; i < counts; i++) {
			int v = s2[first + i*step].values[idx2*2];
			cd->data[cd->size1 + i] = sf*(float)v;
		}
		cd->size2 = counts;
	}
	
	*cd_size = sizeof(calc_data) + (cd->size1+cd->size2)*sizeof(float);

	if (s1_size)
		free(s1);
	if (s2_size)
		free(s2);
}

