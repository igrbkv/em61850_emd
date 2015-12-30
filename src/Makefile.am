## Process this file with automake to produce Makefile.in

bin_PROGRAMS = emd

emd_SOURCES = emd.c emd.h \
	tcp_server.h tcp_server.c \
	log.c log.h debug.h \
	sv_read.c sv_read.h \
	adc_tlv.h adc_tlv.c \
	adc_client.h adc_client.c \
	calc.h calc.c

AM_CFLAGS = -std=gnu99 -D_FILE_OFFSET_BITS=64 -Wall
#for debug: ./configure CFLAGS='-ggdb -O0'

AM_CPPFLAGS = -I$(top_srcdir)

AM_LDFLAGS = -lm -luv -lpcap
#AM_LDFLAGS = -lrt -lpthread -lm -lcrypt
## EOF
