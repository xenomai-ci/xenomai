lib_LTLIBRARIES = libpsos.la

libpsos_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libpsos_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/copperplate/libcopperplate.la

libpsos_la_SOURCES =	\
	init.c		\
	internal.h	\
	pt.c		\
	pt.h		\
	queue.c		\
	queue.h		\
	rn.c		\
	rn.h		\
	sem.c		\
	sem.h		\
	task.c		\
	task.h		\
	tm.c		\
	tm.h		\
	reference.h

libpsos_la_CPPFLAGS =					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include				\
	-I$(top_srcdir)/lib

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libpsos_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
