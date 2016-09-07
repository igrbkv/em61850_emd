#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <uv.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <ifaddrs.h>

#include "emd.h"
#include "log.h"
#include "server.h"
#include "settings.h"

//#include "debug.h"

#define DEFAULT_BACKLOG 128

char emd_ip4_addr[INET_ADDRSTRLEN];

typedef struct {
	uv_write_t req;
	uv_buf_t buf;
} write_req_t;

static void on_close(uv_handle_t* handle);
static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void tcp_server_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
static void on_new_connection(uv_stream_t *server, int status);
static void on_write(uv_write_t* req, int status);
static void on_shutdown(uv_shutdown_t* req, int status);
static void init_emd_ip4_addr();

void on_close(uv_handle_t* handle)
{
	  free(handle);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
	buf->base = (char*) malloc(suggested_size);
	buf->len = suggested_size;
#ifdef DEBUG
	emd_log(LOG_DEBUG, "alloc to read: %lu", suggested_size);
#endif
}

void on_write(uv_write_t* req, int status) 
{
	write_req_t* wr = (write_req_t*)req;

	if (wr->buf.base != NULL)
		free(wr->buf.base);
	free(wr);

	if (status == 0)
		return;
#ifdef DEBUG
	emd_log(LOG_DEBUG, "uv_write error: %s", uv_strerror(status));
#endif

	if (status == UV_ECANCELED)
		return;

	if (status != UV_EPIPE) {
		emd_log(LOG_ERR, "write fatal error! exit.");
		exit(EXIT_FAILURE);
	}

	uv_close((uv_handle_t*)req->handle, on_close);
}

void on_shutdown(uv_shutdown_t* req, int status)
{
	if (status < 0) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "uv_shutdown() failed: %s", uv_strerror(status));
#endif
		uv_close((uv_handle_t*)req->handle, on_close);
		free(req);
	}
}

void tcp_server_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
	write_req_t* wr;
    uv_shutdown_t* req;

	if (nread < 0) {
		if (nread != UV_EOF)
			emd_log(LOG_ERR, "Read error %s", uv_err_name(nread));
#ifdef DEBUG
		emd_log(LOG_DEBUG, "Connection %p closed: %s", stream, uv_err_name(nread));
#endif
		req = (uv_shutdown_t*) malloc(sizeof(*req));
		uv_shutdown(req, stream, on_shutdown);
	} else if (nread > 0) {
		ssize_t offset = 0;
		while (offset < nread) {
			char *out_buf = NULL;
			int out_buf_len;
			ssize_t ret;

			if ((ret = parse_request(&buf->base[offset], nread -offset,
				(void **)&out_buf, &out_buf_len)) == -1) {
				emd_log(LOG_ERR, "Unable to parse input message.");
				return;
			}
			
			wr = (write_req_t*) malloc(sizeof(*wr));
			wr->buf = uv_buf_init(out_buf,out_buf_len);
			uv_write(&wr->req, stream, &wr->buf, 1, on_write);

			offset += ret;
		}
	}
	if (buf->base) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "free to read");
#endif
		free(buf->base);
	}
}

void on_new_connection(uv_stream_t *server, int status)
{
	uv_tcp_t *stream;
	int ret;

	if (status < 0) {
		emd_log(LOG_ERR, "New connection error %s\n", uv_strerror(status));
		// error!
		return;
	}

	stream = malloc(sizeof(uv_tcp_t));

#ifdef DEBUG
	emd_log(LOG_DEBUG, "New connection: %p", stream);
#endif

	if ((ret = uv_tcp_init(uv_default_loop(), stream)) != 0) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "uv_tcp_init() failed: %s", uv_strerror(ret));
#endif
		free(stream);
		return;
	} 
	
	stream->data = server;
	
	if ((ret = uv_accept(server, (uv_stream_t*) stream)) != 0) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "uv_accept() failed: %s", uv_strerror(ret));
#endif
		uv_close((uv_handle_t *)stream, on_close);
		return;
	}
	if ((ret = uv_read_start((uv_stream_t*) stream, alloc_buffer, tcp_server_read)) != 0) {
#ifdef DEBUG
		emd_log(LOG_DEBUG, "uv_accept failed! %s", uv_strerror(ret));
#endif
		uv_close((uv_handle_t *)stream, on_close);
	}
}

void init_emd_ip4_addr()
{
	uv_interface_address_t *info;
	int count, i;
	// FIXME br0 not working without enp1s0
	// Либо
	//  Сделать сервис по типу net, который проверяет
	// наличие enp1s0 и provide mynet
	// Сервис emd сделать зависимым от mynet(need mynet)
	// либо
	// Поправить сервис net.lo
#define MAX_ATTEMPTS 10
	int base_iface_inited = 0;
	for (int j = 0; j < MAX_ATTEMPTS; j++) {
		uv_interface_addresses(&info, &count);
		i = count;
		while (i--) {
			uv_interface_address_t interface = info[i];

			if (!interface.is_internal && 
				interface.address.address4.sin_family == AF_INET &&
				interface.name[0] == 'e') {
				uv_ip4_name(&interface.address.address4, emd_ip4_addr, sizeof(emd_ip4_addr));
				base_iface_inited = 1;
				break;
			}
		}

		uv_free_interface_addresses(info, count);
		if (base_iface_inited)
			break;
		sleep(1);
	}

	// ipv4
	uv_interface_addresses(&info, &count);
	i = count;
	while (i--) {
		uv_interface_address_t interface = info[i];

		if (!interface.is_internal && 
			interface.address.address4.sin_family == AF_INET &&
			(strcmp("br0", interface.name) == 0 || 
			emd_ip4_addr[0] == '\0')) {
			uv_ip4_name(&interface.address.address4, emd_ip4_addr, sizeof(emd_ip4_addr));
			break;
		}
	}

	uv_free_interface_addresses(info, count);
}

void inc_ip4_addr(char *dst, const char *src, int step)
{
	strcpy(dst, src);
	char *p = strrchr(dst, '.');
	if (p == NULL)
		return;

	p++;
	snprintf(p, 4, "%d", atoi(p) + step);
}

int tcp_server_init()
{
	uv_tcp_t *server;
	struct sockaddr_in addr;

	init_emd_ip4_addr();
		
	server = (uv_tcp_t*) malloc(sizeof(*server));
	
	uv_tcp_init(uv_default_loop(), server);

	uv_ip4_addr(emd_ip4_addr, emd_port, &addr);

	uv_tcp_bind(server, (const struct sockaddr*)&addr, 0);
	int ret = uv_listen((uv_stream_t*) server, DEFAULT_BACKLOG, on_new_connection);
	if (ret) {
		emd_log(LOG_ERR, "Listen error %s", uv_strerror(ret));
		return -1;
	}
	return 0;
}

int tcp_server_close()
{
	return 0;
}
