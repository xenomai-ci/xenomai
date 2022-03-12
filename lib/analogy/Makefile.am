lib_LTLIBRARIES = libanalogy.la

libanalogy_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 1:0:0 -lm

libanalogy_la_LIBADD = @XENO_CORE_LDADD@

libanalogy_la_SOURCES =	\
	async.c		\
	descriptor.c	\
	info.c		\
	internal.h	\
	math.c		\
	calibration.c	\
	calibration.h	\
	range.c		\
	root_leaf.h	\
	sync.c		\
	sys.c

libanalogy_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include 	\
	-I$(top_srcdir)/lib/boilerplate	

AM_LIBTOOLFLAGS = --silent
