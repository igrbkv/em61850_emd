## Process this file with automake to produce Makefile.in

bin_PROGRAMS = emd

emd_SOURCES = ../config.h \
	emd.c emd.h \
	tcp_server.h tcp_server.c \
	server.h server.c \
	log.c log.h debug.h \
	sv_read.c sv_read.h \
	tlv.h tlv.c \
	sock_tlv.h sock_tlv.c \
	adc_client.h adc_client.c \
	sync_client.h sync_client.c \
	calc.h calc.c \
	calc_comparator.c \
	calc_data.c \
	calc_ui.c \
	calc_harm.c \
	calc_p.c calc_a.c \
	calc_math.h calc_math.c \
	calib.h calib.c \
	settings.h settings.c \
	streams_list.h streams_list.c \
	upgrade.h upgrade.c \ 
	proto.h

AM_CFLAGS = -std=gnu99 -D_FILE_OFFSET_BITS=64 -Wall
#for debug: ./configure CFLAGS='-ggdb -O0'

#AM_CPPFLAGS = -I$(top_srcdir)
AM_CPPFLAGS = -include $(top_srcdir)/config.h

AM_LDFLAGS = -lpthread -lm -luv -lpcap
#AM_LDFLAGS = -lrt -lpthread -lm -lcrypt
## EOF
