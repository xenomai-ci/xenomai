lib_LTLIBRARIES = libsmokey.la

libsmokey_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0
libsmokey_la_LIBADD = @XENO_CORE_LDADD@

libsmokey_la_SOURCES =	\
	helpers.c	\
	init.c

libsmokey_la_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib
