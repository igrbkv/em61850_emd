#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <pcap.h>
#include <errno.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <net/if.h> //ifreq, IF_NAMESIZE
#include <netinet/if_ether.h> /* includes net/ethernet.h */
#include <uv.h>		// for uv_mutex_t
#include <sched.h>

#include "emd.h"
#include "sv_read.h"
#include "log.h"
#include "settings.h"
#include "adc_client.h"



/* Алгоритм приема данных:
 * pdu => cur (second) => ready (2 seconds)
 */

#define FREQUENCY 50
#define SMP_BY_SECOND_MAX 1280*FREQUENCY
#define BUFFERED_SECONDS 4

#ifndef LOCAL_DEBUG
#define PROCESSED_MAX 0	// infinity 
#else
#define PROCESSED_MAX 16000	// 10 sec
#define BUFFER_INCREMENT 1*1024*1024
#define BUFFER_LOW_LIMIT 1024*32
#endif

#define ASDU_MAX 16

// Tags
#define	T_SAV_PDU 0x60
#define	T_NO_ASDU 0x80
#define	T_SEQ_ASDU 0xa2
#define	T_ASDU 0x30
#define	T_ID  0x80
#define	T_SMP_CNT 0x82
#define	T_CONF_REV 0x83
#define	T_SMP_SYNC 0x85
#define	T_SMP_RATE 0x86
#define	T_DATA 0x87

typedef struct {
	union {
		struct timeval last_smp_ts;	// in cur[]
		struct timeval ts;			// in ready[]
	};
	uint16_t smp_cnt;
	uint16_t last_smp;
	uint16_t rate;
	sv_data data[SMP_BY_SECOND_MAX];
} sv_data_second;

typedef struct {
	u_int16_t smp_cnt;
	u_char smp_sync;
	sv_data data;
} asdu;

typedef struct {
	struct timeval ts;
	int no_asdu;
	int cur_asdu;
	uint16_t rate;
	asdu asdus[ASDU_MAX];
} sv_pdu;

typedef struct {
	u_int64_t total;
	u_int64_t sv;
	u_int64_t err_packet;
	u_int64_t err_type;
	u_int64_t err_size;
	u_int64_t err_sv_id;
	u_int64_t smp_dropped[2];
	u_int64_t timeout[2];
} statistic;

struct _sv_header {
	struct ether_header eh;
	u_int16_t vlan_id;
	u_int16_t type;
	u_int16_t app_id;
	u_int16_t len;
} __attribute__ ((__packed__));
typedef struct _sv_header sv_header;

char emd_mac[17];	
static char ifname[IF_NAMESIZE];

static sv_data_second *ready;
static sv_data_second *cur;
static statistic *st;
static char *sv_id[2];
static int idx;
static char *errbuf;
static pcap_t *descr;
static sv_pdu *pdu;
static struct timeval timeout;
static struct timeval err_threshold;
static struct timeval stream1_alive_ts, stream2_alive_ts;

#ifdef LOCAL_DEBUG
static char *buffer;
static size_t buffer_size;
static size_t buffer_cur_idx;
#else
static uv_mutex_t mutex;
static uv_thread_t thread;
#endif

static int time_equal(int i1, int i2);
static void run(void *arg);
static void pcap_callback(u_char *useless, 
	const struct pcap_pkthdr *pkthdr,
	const u_char *packet);
static int parse_tlv(const u_char *p, int *size);
static void pdu_to_cur();
static void cur_to_ready();
#ifdef LOCAL_DEBUG 
static void print_statistic(bool finish);
#endif

int sv_read_init()
{
	emd_log(LOG_INFO, "sv_read_init");
	emd_mac[0] = '\0';
	ifname[0] = '\0';
	pcap_if_t *alldevs;
	pcap_findalldevs(&alldevs, errbuf);

	for (pcap_if_t *d = alldevs; d; d = d->next) {
		if (d->flags & PCAP_IF_LOOPBACK)
			continue;
		else if (d->addresses) {
			emd_log(LOG_DEBUG, "found iface: %s", d->name);
			if (d->name[0] == 'e') {
				// вешаемся на первый попавшийся интерфейс
				strncpy(ifname, d->name, IF_NAMESIZE-1);
				break;		
			}
		}
	}
	pcap_freealldevs(alldevs);

	if (ifname[0] == '\0') {
		emd_log(LOG_ERR, "pcap did not find iface device");
		return -1;
	} else {
		int fd;
		struct ifreq ifr;

		memset(&ifr, 0, sizeof(ifr));
		if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
			emd_log(LOG_DEBUG, "socket() failed: %s", strerror(errno));
			ifname[0] ='\0';
			return -1;
		}
		ifr.ifr_addr.sa_family = AF_INET;
		strncpy(ifr.ifr_name , ifname , IF_NAMESIZE-1);
		if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
			emd_log(LOG_DEBUG, "ioctl() failed: %s", strerror(errno));
			ifname[0] = '\0';
			close(fd);
			return -1;
		} else {
			char buf[32];
			snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
				(uint8_t)ifr.ifr_hwaddr.sa_data[0],
				(uint8_t)ifr.ifr_hwaddr.sa_data[1],
				(uint8_t)ifr.ifr_hwaddr.sa_data[2],
				(uint8_t)ifr.ifr_hwaddr.sa_data[3],
				(uint8_t)ifr.ifr_hwaddr.sa_data[4],
				(uint8_t)ifr.ifr_hwaddr.sa_data[5]);
			memcpy(emd_mac, buf, 17);
		}

		close(fd);
	}

	ready =(sv_data_second *)calloc(2*2, sizeof(sv_data_second));
	memset(ready, 0, sizeof(sv_data_second)*2*2);
	cur = (sv_data_second *)calloc(2, sizeof(sv_data_second));
	memset(cur, 0, sizeof(sv_data_second)*2);
	st = (statistic *)malloc(sizeof(statistic));
	memset(st, 0, sizeof(statistic));
	pdu = (sv_pdu *)malloc(sizeof(sv_pdu)); 
	errbuf = (char *)malloc(PCAP_ERRBUF_SIZE);
	timeout.tv_sec = 0;
	timeout.tv_usec = 500000; // 1/2 second
	err_threshold.tv_sec = 0;
	err_threshold.tv_usec = 100000; // 100 ms
#ifdef LOCAL_DEBUG
	buffer_size = BUFFER_INCREMENT;
	buffer = (char *)malloc(buffer_size);
	buffer_cur_idx = 0;
#else
	uv_mutex_init(&mutex);
#endif

	read_start();

	return 0;
}

int sv_read_close()
{
	if (descr) { 
		pcap_breakloop(descr);
#ifndef LOCAL_DEBUG
		uv_thread_join(&thread);
#endif
	}
	free(ready);
	free(cur);
	free(pdu);
	free(errbuf);
	free(st);
#ifndef LOCAL_DEBUG
	uv_mutex_destroy(&mutex);
#endif

	ifname[0] = '\0';
	return 0;
}

// Возвращает состояния потоков
// 0/1 - нет/есть
// @s1 - поток1 
// @s2 - поток2 
void stream_states(int *s1, int *s2)
{
	struct timeval tv1, tv2;
	uv_mutex_lock(&mutex);
	tv1 = cur[0].ts;
	tv2 = cur[1].ts;
	uv_mutex_unlock(&mutex);
	if (s1)
		*s1 = (memcmp(&tv1, &stream1_alive_ts, sizeof(tv1)) != 0);

	if (s2)
		*s2 = (memcmp(&tv2, &stream2_alive_ts, sizeof(tv2)) != 0);

	stream1_alive_ts = tv1;
	stream2_alive_ts = tv2;
}

int read_start()
{
	if (descr) { 
		pcap_breakloop(descr);
#ifndef LOCAL_DEBUG
		uv_thread_join(&thread);
#endif
	}

	int used = 0;
	if (streams_prop.stream1 == 0) {
		if (memcmp(emd_mac, adc_prop.dst_mac, 17) == 0)
			used = 1;
	} else
		used = 1;
	
	if (!used && streams_prop.stream2 == 1) 
		used = 1;

	if (!used)
		return 0;

	char *filter = NULL;
	char smac1[17+1] = {0}, smac2[17+1] = {0};
	char *fmt[] = {
		"vlan 32768 and ether dst %s",
		"vlan 32768 and ether dst %s and ether src %s",
		"vlan 32768 and ether dst %s and (ether src %s or ether src %s)"
	};
	if (streams_prop.stream1 == 0) {
		strncpy(smac1, adc_prop.src_mac, 17);
		sv_id[0] = strdup(adc_prop.sv_id);
	}
	else {
		sv_id[0] = strdup(streams_prop.sv_id1);
		if (streams_prop.mac1[0] != '\0')
			strncpy(smac1, streams_prop.mac1, 17);
	}

	if (streams_prop.stream2 == 1) {
		sv_id[1] = strdup(streams_prop.sv_id2);
		if (streams_prop.mac2[0] != '\0')
			strncpy(smac1[0] == '\0'? smac1: smac2, streams_prop.mac2, 17);
	}

	if (smac1[0] && smac2[0])
		asprintf(&filter, fmt[2], emd_mac, smac1, smac2);
	else if (smac1[0])
		asprintf(&filter, fmt[1], emd_mac, smac1);
	else
		asprintf(&filter, fmt[0], emd_mac);
	
	emd_log(LOG_INFO, "Filter: %s", filter);
#ifdef LOCAL_DEBUG
	run(filter);
#else
	uv_thread_create(&thread, run, filter);
	int ret;
	if ((ret = pthread_setschedprio(thread, sched_get_priority_max(sched_getscheduler(thread)))) != 0) {
		emd_log(LOG_ERR, "pthread_setschedprio() failed: %d", ret);
	}
#endif
	return 0;
}

void run(void *arg)
{
	char *filter = (char *)arg;

	struct bpf_program fp;
    descr = pcap_open_live(ifname, BUFSIZ, 1, -1, errbuf); 
	if (descr == NULL) {
		emd_log(LOG_DEBUG, "pcap_open_live(): %s\n", errbuf);
		goto err;
	}

	bpf_u_int32 maskp;          /* subnet mask */
	bpf_u_int32 netp;           /* ip */
	pcap_lookupnet(ifname, &netp, &maskp, errbuf);

	if(pcap_compile(descr, &fp, filter, 0, netp) == -1) {
		emd_log(LOG_DEBUG, "Error calling pcap_compile(%s): %s", filter, errbuf);
		goto err;
	}

	if(pcap_setfilter(descr, &fp) == -1) { 
		emd_log(LOG_DEBUG, "Error setting filter"); 
		goto err;
	}
	
	pcap_loop(descr, PROCESSED_MAX, pcap_callback, NULL);

err:
	free(filter);
	pcap_close(descr);
	descr = NULL;

	free(sv_id[0]);
	free(sv_id[1]);
	memset(sv_id, 0, sizeof(sv_id));
}

void pcap_callback(u_char *useless, 
	const struct pcap_pkthdr *pkthdr,
	const u_char *packet)
{
	st->total++;

	if (pkthdr->len < sizeof(sv_header)) {
		st->err_packet++;
		return;
	}
	sv_header *svh = (sv_header *)packet;
	int len = ntohs(svh->len);
	if (ntohs(svh->type) != 0x88BA) {
		st->err_type++;
		return;
	}
	
	// в pdu индекс не меняется, потому достаточно
	// взять первый и не сравнивать sv_id.
	idx = -1;

	len -= 4;
	pdu->ts = pkthdr->ts;
	// FIXME to distinguish T_NO_ASDU and T_ID
	pdu->no_asdu = 0;

	int ret = parse_tlv(packet + pkthdr->len - len, &len);
	if (ret == 0) {
		st->sv++;
		pdu_to_cur();
	}
}

int parse_tlv(const u_char *p, int *size)
{
	int tag = *p;
	p++;
	(*size)--;
	int len;
	int sz_len;
	if (*p & 0x80) {
		sz_len = (*p & 0x7f)+1;
		len = *(p+1);
		for (int i = 2; i < sz_len; i++) {
			len <<= 8;
			len += *(p+i);
		}
	} else {
		sz_len = 1;
		len = *p;
	}
	p += sz_len;
	(*size) -= sz_len;
	if ((*size)-len < 0) {
		st->err_size++;
		return -1;
	}
	switch (tag) {
		case T_SAV_PDU:
			break;
		case T_NO_ASDU:
			if (pdu->no_asdu == 0) {
				pdu->no_asdu = *p;
			}
			else {/* case T_ID: */
				if (idx != -1)
					break;
				if (strncmp((const char *)p, sv_id[0], len) == 0)
					idx = 0;
				else if (strncmp((const char *)p, sv_id[1], len) == 0)
					idx = 1;
				else {
					st->err_sv_id++;
					return -1;
				}
			}
			break;
		case T_SEQ_ASDU:
			pdu->cur_asdu = -1;
			break;
		case T_ASDU:
			pdu->cur_asdu++;
			return parse_tlv(p, size); 
		case T_SMP_CNT:
			pdu->asdus[pdu->cur_asdu].smp_cnt = ntohs(*(u_int16_t *)p);
			break;
		case T_CONF_REV:
			break;
		case T_SMP_RATE:
			// FIXME smp_rate must be by second, not by period
			pdu->rate = ntohs(*(u_int16_t *)p) * FREQUENCY;
			break;
		case T_SMP_SYNC:
			pdu->asdus[pdu->cur_asdu].smp_sync = *p;
			break;
		case T_DATA:
			for (int i = 0; i < VALUES_NUM; i++) {
				pdu->asdus[pdu->cur_asdu].data.values[i] = ntohl(*((int32_t *)(p + sizeof(int32_t)*i)));
			}
			break;
	}
	if (~tag & 0x60 && ~tag & 0x20) {
		p += len;
		(*size) -= len;
	}
	if (*size)
		return parse_tlv(p, size);
	return 0;
}

void pdu_to_cur()
{
	struct timeval tv;
	u_int16_t rate = pdu->rate;
	timersub(&pdu->ts, &cur[idx].last_smp_ts, &tv);
	
#ifndef LOCAL_DEBUG
	uv_mutex_lock(&mutex);
#endif

	// rate changed or timeout
	if (rate != cur[idx].rate || tv.tv_sec < 0) {
		memset(&cur[idx], 0, sizeof(sv_data_second));
		cur[idx].rate = rate;

	} else if (timercmp(&tv, &timeout, >)) {
		st->timeout[idx]++;
		memset(&cur[idx], 0, sizeof(sv_data_second));
		cur[idx].rate = rate;
	// dropped last packet in second
	} else if (cur[idx].last_smp > pdu->asdus[0].smp_cnt){
		st->smp_dropped[idx] += (u_int64_t)cur[idx].rate - (u_int64_t)cur[idx].smp_cnt;
		cur_to_ready();
		cur[idx].smp_cnt = cur[idx].last_smp = 0;
	}

	for (int i = 0; i < pdu->no_asdu; i++) {
		cur[idx].data[pdu->asdus[i].smp_cnt] = pdu->asdus[i].data;
	}
	cur[idx].smp_cnt += pdu->no_asdu;
	cur[idx].last_smp_ts = pdu->ts;
	cur[idx].last_smp = pdu->asdus[pdu->no_asdu-1].smp_cnt;
	if (cur[idx].last_smp == cur[idx].rate - 1) {
		st->smp_dropped[idx] += (u_int64_t)cur[idx].rate - (u_int64_t)cur[idx].smp_cnt;
		cur_to_ready();
		cur[idx].smp_cnt = cur[idx].last_smp = 0;
	}

#ifndef LOCAL_DEBUG
	uv_mutex_unlock(&mutex);
#endif
}

void cur_to_ready()
{
#ifdef LOCAL_DEBUG 
	print_statistic();
#endif

	struct timeval tv = {0, 1000000/cur[idx].rate*cur[idx].last_smp};
	// last sample time stamp => second time stamp
	timersub(&cur[idx].last_smp_ts, &tv, &tv);


	if (timercmp(&ready[idx*2 + 0].ts, &tv, >))
		// system time was corrected => clean data
		memset(&ready[idx*2 + 1], 0, sizeof(sv_data_second));
	else
		memcpy(&ready[idx*2 + 1], &ready[idx*2 + 0], sizeof(sv_data_second));
	memcpy(&ready[idx*2 + 0], &cur[idx], sizeof(sv_data_second));
	ready[idx*2 + 0].ts = tv;
}

int time_equal(int i1, int i2)
{
	struct timeval tv, err_threshold = {0, 100000};	// 100 ms
	if (timercmp(&ready[i1].ts, &ready[i2].ts, >)) {
		timersub(&ready[i1].ts, &ready[i2].ts, &tv);
		if (timercmp(&tv, &err_threshold, < ))
			return 1;
	} else {
		timersub(&ready[i2].ts, &ready[i1].ts, &tv);
		if (timercmp(&tv, &err_threshold, < ))
			return 1;
	}
	return 0;
}

int sv_get_ready(struct timeval *ts, sv_data **stream1, int *stream1_size, sv_data **stream2, int *stream2_size)
{
	sv_data_second *s1, *s2;
	sv_data *stm1, *stm2;

	if (stream1) {
		s1 = malloc(sizeof(sv_data_second));
		s1->rate = 0;
	} else s1 = NULL;
	if (stream2) {
		s2 = malloc(sizeof(sv_data_second));
		s2->rate = 0;
	} else s2 = NULL;
	int i1 = -1, i2 = -1;

#ifndef LOCAL_DEBUG
	uv_mutex_lock(&mutex);
#endif

	if(stream1 && !stream2)
		*s1 = ready[0];
	else if (!stream1 && stream2)
		*s2 = ready[2];
	else if (stream1 && stream2) {
		if (time_equal(0, 2)) {
			i1 = 0; i2 = 2;
		} else if (time_equal(0, 3)) {
			i1 = 0; i2 = 3;
		} else if (time_equal(1, 2)) {
			i1 = 1; i2 = 2;
		} else if (time_equal(1, 3)) {
			i1 = 1; i2 = 3;
		}
		if (i1 != -1 && (ready[i1].ts.tv_sec || ready[i1].ts.tv_usec))
			memcpy(s1, &ready[i1], sizeof(sv_data_second));
		if (i2 != -1 && (ready[i2].ts.tv_sec || ready[i2].ts.tv_usec))
			memcpy(s2, &ready[i2], sizeof(sv_data_second));
	}

#ifndef LOCAL_DEBUG 
	uv_mutex_unlock(&mutex);
#endif

	*ts = (struct timeval){0, 0};
	if (stream1) {
		if (s1->rate) {
			*ts = s1->ts;
			stm1 = malloc(s1->rate * sizeof(*stm1));
			*stream1_size = s1->rate;
			for (int i = 0; i < s1->rate; i++)
				stm1[i] = s1->data[i];
			*stream1 = stm1;
		}
		else {
			*stream1 = NULL;
			*stream1_size = 0;
		}
	}
	if (stream2) {
		if (s2->rate) {
			*ts = s2->ts;
			stm2 = malloc(s2->rate * sizeof(*stm2));
			*stream2_size = s2->rate;
			for (int i = 0; i < s2->rate; i++)
				stm2[i] = s2->data[i];
			*stream2 = stm2;
		}
		else {
			*stream2 = NULL;
			*stream2_size = 0;
		}
	}
	
	free(s1);
	free(s2);

	return 0;
}

#ifdef LOCAL_DEBUG 
void print_statistic(bool finish)
{
	if (buffer_size < buffer_cur_idx + BUFFER_LOW_LIMIT) {
		buffer_size += BUFFER_INCREMENT;
		buffer = (char *)realloc(buffer, buffer_size);
	}

	buffer_cur_idx += sprintf(&buffer[buffer_cur_idx], 
		"total: %lu\n"
		"sv: %lu\n"
		"err_packet: %lu\n"
		"err_type: %lu\n"
		"err_size: %lu\n"
		"err_sv_id: %lu\n"
		"smp_dropped: %lu %lu\n"		
		"timeout: %lu %lu\n",  
		st->total,
		st->sv,
		st->err_packet,
		st->err_type,
		st->err_size,
		st->err_sv_id,
		st->smp_dropped[0], st->smp_dropped[1],
		st->timeout[0], st->timeout[1]);

	if (finish) {
		printf("%s\n", buffer);
		free(buffer);
		buffer = NULL;
	}
}

int main(int argc, char *argv[])
{
	char *dst_mac1 = NULL;	//("08:9e:01:71:be:af");
	char *src_mac1 = NULL;
	char *sv_id1 = "EM61850_8"; 
	char *dst_mac2 = NULL;
	char *src_mac2 = NULL;
	char *sv_id2 = NULL;

	sv_read_init();
	sv_read_start(dst_mac1, src_mac1, sv_id1, dst_mac2, src_mac2, sv_id2);
	print_statistic(true);
	sv_read_close();

	exit(EXIT_SUCCESS);
}
#endif
