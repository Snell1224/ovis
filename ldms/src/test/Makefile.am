ACLOCAL_AMFLAGS = -I m4
CORE = ../core

sbin_PROGRAMS = test_ldms_xprt_reconnect

AM_CFLAGS = -I$(srcdir)/$(CORE) -I.. -I$(top_srcdir)/lib/src
AM_LDFLAGS =

sbin_PROGRAMS += test_ldms_xprt_reconnect
test_ldms_xprt_reconnect_SOURCES = test_ldms_xprt_reconnect.c
test_ldms_xprt_reconnect_LDADD = $(CORE)/libldms.la
test_ldms_xprt_reconnect_LDFLAGS = $(AM_LDFLAGS) -lpthread
test_ldms_xprt_reconnect_CFLAGS = $(AM_CFLAGS)

sbin_PROGRAMS += test_ldms_notify
test_ldms_notify_SOURCES = test_ldms_notify.c
test_ldms_notify_LDADD = $(CORE)/libldms.la
test_ldms_notify_LDFLAGS = $(AM_LDFLAGS) -pthread
test_ldms_notify_CFLAGS = $(AM_CFLAGS)

sbin_PROGRAMS += test_ldms_xprt_send_recv
test_ldms_xprt_send_recv_SOURCES = test_ldms_xprt_send_recv.c
test_ldms_xprt_send_recv_LDADD = $(CORE)/libldms.la
test_ldms_xprt_send_recv_LDFLAGS = $(AM_LDFLAGS) -pthread
test_ldms_xprt_send_recv_CFLAGS = $(AM_CFLAGS)

sbin_PROGRAMS += test_ldms_push
test_ldms_push_SOURCES = test_ldms_push.c
test_ldms_push_LDADD = $(CORE)/libldms.la
test_ldms_push_LDFLAGS = $(AM_LDFLAGS) -pthread
test_ldms_push_CFLAGS = $(AM_CFLAGS)

sbin_PROGRAMS += test_ldms
test_ldms_SOURCES = test_ldms.c
test_ldms_LDADD = $(CORE)/libldms.la
test_ldms_LDFLAGS = $(AM_LDFLAGS) -pthread \
		    $(top_builddir)/lib/src/ovis_util/libovis_util.la
test_ldms_CFLAGS = $(AM_CFLAGS)

sbin_PROGRAMS += test_ldms_set_info
test_ldms_set_info_SOURCES = test_ldms_set_info.c
test_ldms_set_info_LDADD = $(CORE)/libldms.la
test_ldms_set_info_LDFLAGS = $(AM_LDFLAGS) -pthread
test_ldms_set_info_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS = test_metric
test_metric_SOURCES = test_metric.c
test_metric_LDADD = $(CORE)/libldms.la
test_metric_LDFLAGS = $(AM_LDFLAGS) -pthread -lm
test_metric_CFLAGS = $(AM_CFLAGS)

TESTS = $(check_PROGRAMS)
