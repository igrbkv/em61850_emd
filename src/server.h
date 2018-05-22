#ifndef SERVER_H_
#define SERVER_H_

int parse_request(void *handle, void *in, int in_len, void **out, int *out_len);

struct stream_data {
	int state;
};
void init_stream_data(struct stream_data *sd);

const char *get_proto_ver();

#endif
