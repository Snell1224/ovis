ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = zap_test
AM_CFLAGS = -I$(srcdir)/../ -I$(top_srcdir)/lib/src -I$(top_srcdir) -I../..

zap_test_SOURCES = zap_test.c
zap_test_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_big
zap_test_big_SOURCES = zap_test_big.c
zap_test_big_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_reconnect
zap_test_reconnect_SOURCES = zap_test_reconnect.c
zap_test_reconnect_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_disconnect
zap_test_disconnect_SOURCES = zap_test_disconnect.c
zap_test_disconnect_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_reconnect_many
zap_test_reconnect_many_SOURCES = zap_test_reconnect_many.c
zap_test_reconnect_many_LDADD = $(builddir)/../libzap.la -lpthread -ldl

sbin_PROGRAMS += zap_test_shared_map
zap_test_shared_map_SOURCES = zap_test_shared_map.c
zap_test_shared_map_LDADD = $(builddir)/../libzap.la -lpthread -ldl
