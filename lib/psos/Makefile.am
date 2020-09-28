lib_LTLIBRARIES = libpsos@CORE@.la

libpsos@CORE@_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libpsos@CORE@_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/copperplate/libcopperplate@CORE@.la

libpsos@CORE@_la_SOURCES =	\
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

libpsos@CORE@_la_CPPFLAGS =					\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include				\
	-I$(top_srcdir)/lib

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libpsos@CORE@_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
