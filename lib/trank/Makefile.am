
lib_LTLIBRARIES = libtrank.la

libtrank_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libtrank_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/alchemy/libalchemy.la

libtrank_la_SOURCES =	\
	init.c		\
	internal.c	\
	internal.h	\
	posix.c		\
	native.c

libtrank_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@	\
	-D__XENO_COMPAT__	\
	-I$(top_srcdir)/include	\
	-I$(top_srcdir)/lib
