
lib_LTLIBRARIES = libtrank@CORE@.la

libtrank@CORE@_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libtrank@CORE@_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/alchemy/libalchemy@CORE@.la

libtrank@CORE@_la_SOURCES =	\
	init.c		\
	internal.c	\
	internal.h	\
	posix.c		\
	native.c

libtrank@CORE@_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@	\
	-D__XENO_COMPAT__	\
	-I$(top_srcdir)/include	\
	-I$(top_srcdir)/lib
