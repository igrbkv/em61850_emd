#ifndef SV_READ_H_
#define SV_READ_H_

#define FREQUENCY 50
#define RATE_MAX 256*FREQUENCY
#define VALUES_NUM 16

typedef struct {
	union {
		int32_t values[VALUES_NUM];
		struct {
			int32_t ia;
			int32_t qia;
			int32_t ib;
			int32_t qib;
			int32_t ic;
			int32_t qic;
			int32_t in;
			int32_t qin;
			int32_t ua;
			int32_t qua;
			int32_t ub;
			int32_t qub;
			int32_t uc;
			int32_t quc;
			int32_t un;
			int32_t qun;
		};
	};
} sv_data;

struct timeval;

int sv_read_init();
int sv_read_close();
int read_start();
int sv_get_ready(struct timeval *ts, sv_data **stream1, int *stream1_size, sv_data **stream2, int *stream2_size);

void stream_states(int *s1, int *s2);

extern char emd_mac[17];
#endif
