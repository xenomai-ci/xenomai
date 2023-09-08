noinst_LTLIBRARIES = libarch.la

libarch_la_LDFLAGS = @XENO_LIB_LDFLAGS@

libarch_la_SOURCES = features.c sigreturn.c

libarch_la_CPPFLAGS =			\
	@XENO_COBALT_CFLAGS@ 		\
	-I$(srcdir)/../..		\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include

SUBDIRS = include
