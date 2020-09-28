sbin_PROGRAMS = rtcanconfig

bin_PROGRAMS = rtcanrecv rtcansend

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

AM_CPPFLAGS = 						\
	@XENO_USER_CFLAGS@ 				\
	-I$(top_srcdir)/include

AM_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

rtcanconfig_SOURCES = rtcanconfig.c

rtcanconfig_LDADD = \
	 @XENO_CORE_LDADD@		\
	 @XENO_USER_LDADD@		\
	-lpthread -lrt

rtcanrecv_SOURCES = rtcanrecv.c

rtcanrecv_LDADD = \
	../../lib/alchemy/libalchemy@CORE@.la \
	../../lib/copperplate/libcopperplate@CORE@.la \
	@XENO_CORE_LDADD@		\
	@XENO_USER_LDADD@		\
	-lpthread -lrt

rtcansend_SOURCES = rtcansend.c

rtcansend_LDADD = \
	../../lib/alchemy/libalchemy@CORE@.la \
	../../lib/copperplate/libcopperplate@CORE@.la \
	@XENO_CORE_LDADD@		\
	@XENO_USER_LDADD@		\
	-lpthread -lrt
