testdir = @XENO_TEST_DIR@
test_PROGRAMS = smokey_net_server

noinst_LIBRARIES = libnet_common.a
noinst_HEADERS = \
	smokey_net.h \
	smokey_net_server.h

AM_CPPFLAGS = \
	@XENO_USER_CFLAGS@ \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/kernel/drivers/net/stack/include

libnet_common_a_SOURCES = \
	client.c \
	server.c \
	setup.c

smokey_net_server_SOURCES = \
	smokey_net_server.c

smokey_net_server_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@

smokey_net_server_LDADD = \
	libnet_common.a \
	@XENO_CORE_LDADD@ \
	@XENO_USER_LDADD@ \
	-lpthread -lrt
