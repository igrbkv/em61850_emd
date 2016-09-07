#include <stdlib.h>
#include <string.h>
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
	*cd = NULL;

	sv_get_ready(ts, stream1? &s1: NULL, stream1? &s1_size: NULL, stream2? &s2: NULL, stream2? &s2_size: NULL);

	if (s1_size == 0 && s2_size == 0)
		return;

	int buf_size = sizeof(struct calc_data);

	if (s1_size && s2_size)
		buf_size += s1_size + s2_size;
	else if (s1_size)
		buf_size += s1_size*2;
	else if (s2_size)
		buf_size += s2_size*2;

	struct calc_data *_cd = (struct calc_data *)malloc( sizeof(float)*buf_size);
	memset(_cd, 0, sizeof(struct calc_data));

	if (s1_size) {
		float sf = scaleFactor(idx1);
		int first = s1_size/scale*begin;
		int step = 1;
		int counts = length*s1_size/scale;
		// шаг прореживания
		while (counts/step > counts_limit)
			step++;
		counts /= step;

		for (int i = 0; i < counts; i++) {
			int v = s1[first + i*step].values[idx1*2];
			_cd->data[i] = sf*(float)v;
		}
		_cd->size1 = counts;

		if (!stream2) {
			for (int i = 0; i < counts; i++) {
				int v = s1[first + i*step].values[idx2*2];
				_cd->data[_cd->size1 + i] = sf*(float)v;
			}
			_cd->size2 = counts;
		}
	}
	
	if (s2_size) {
		int idx;
		int first = s2_size/scale*begin;
		int step = 1;
		int counts = length*s2_size/scale;
		// шаг прореживания
		while (counts/step > counts_limit)
			step++;
		counts /= step;

		if (!stream1) {
			idx = idx1 % STREAM2_START_IDX;
			float sf = scaleFactor(idx1);

			for (int i = 0; i < counts; i++) {
				int v = s2[first + i*step].values[idx*2];
				_cd->data[i] = sf*(float)v;
			}
			_cd->size1 = counts;
		}
		idx = idx2 % STREAM2_START_IDX;
		double sf = scaleFactor(idx);
			for (int i = 0; i < counts; i++) {
				int v = s2[first + i*step].values[idx*2];
				_cd->data[_cd->size1 + i] = sf*(float)v;
			}
			_cd->size2 = counts;
	}
	
	*cd_size = sizeof(struct calc_data) + 
		(_cd->size1 + _cd->size2)*sizeof(float);
	*cd = _cd;

	if (s1_size)
		free(s1);
	if (s2_size)
		free(s2);
}

