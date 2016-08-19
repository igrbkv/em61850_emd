#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_
extern int tcp_server_init();
extern int tcp_server_close();
extern void inc_ip4_addr(char *dst, const char *src, int step);

extern const char emd_ip4_addr[INET_ADDRSTRLEN];
#endif
