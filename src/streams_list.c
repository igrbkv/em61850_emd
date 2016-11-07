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
#include <strings.h>
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

struct _sv_header {
	u_int16_t app_id;
	u_int16_t len;
	u_int16_t null[2];
} __attribute__ ((__packed__));
typedef struct _sv_header sv_header;

static int idx;
static sv_pdu *pdu;

static void pcap_callback(u_char *useless, 
	const struct pcap_pkthdr *pkthdr,
	const u_char *packet);
static int parse_tlv(const u_char *p, int *size);

// @param sp array of stream_property's
// @param count size of array/num elements filled
// @return -1 - error/0 - ok
int scan_streams(stream_property *sp, int *count)
{
	int ret = -1;
	char *filter = "ether [0:4] = 0x010ccd04 and (vlan or ether proto 35002)",

	char *errbuf = (char *)malloc(PCAP_ERRBUF_SIZE);
    pcap_t *descr = pcap_open_live(emd_interface_name, BUFSIZ, 1, 1000, errbuf); 
	if (descr == NULL) {
		emd_log(LOG_DEBUG, "pcap_open_live(): %s\n", errbuf);
		goto err;
	}

	bpf_u_int32 maskp;          /* subnet mask */
	bpf_u_int32 netp;           /* ip */
	pcap_lookupnet(emd_interface_name, &netp, &maskp, errbuf);
	struct bpf_program fp;

	if(pcap_compile(descr, &fp, filter, 0, netp) == -1) {
		emd_log(LOG_DEBUG, "Error calling pcap_compile(%s): %s", filter, errbuf);
		goto err;
	}

	if(pcap_setfilter(descr, &fp) == -1) { 
		emd_log(LOG_DEBUG, "Error setting filter"); 
		goto err;
	}
	
	pcap_dispatch(descr, 0/* infinity */, pcap_callback, NULL);
	ret = 0;

err:
	if (descr)
		pcap_close(descr);
	return ret;
}

void pcap_callback(u_char *useless, 
	const struct pcap_pkthdr *pkthdr,
	const u_char *packet)
{
	st->total++;

	if (pkthdr->len < sizeof(struct ether_header)) {
		st->err_packet++;
		return;
	}

	struct ether_header *eh = (struct ether_header *)packet;
	int off = 0;
	u_int16_t type = ntohs(eh->ether_type);
	if (type == 0x8100) {
		type = ntohs(*(u_int16_t *)(packet + sizeof(*eh) + sizeof(u_int16_t)));
		off = 4;
	}
	if (type != 0x88BA) {
		st->err_type++;
		return;
	} 

	sv_header *svh = (struct sv_header *)(packet + sizeof(*eh) + off);
	if (ntohs(svh->app_id) != 0x4000) {
		st->err_type++;
		return;
	}
	int len = ntohs(svh->len);
	
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
				// 1=>80 8=>256
				pdu->rate = (*p == 1? 80: 256) * FREQUENCY;
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
			if (pdu->rate > RATE_MAX) {
				return -1;
			}
			break;
		case T_SMP_SYNC:
			break;
		case T_DATA:
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


