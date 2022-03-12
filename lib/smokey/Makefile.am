lib_LTLIBRARIES = libsmokey@CORE@.la

libsmokey@CORE@_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0
libsmokey@CORE@_la_LIBADD = @XENO_CORE_LDADD@

libsmokey@CORE@_la_SOURCES =	\
	helpers.c	\
	init.c

libsmokey@CORE@_la_CPPFLAGS =			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

AM_LIBTOOLFLAGS = --silent
