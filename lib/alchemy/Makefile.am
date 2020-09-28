lib_LTLIBRARIES = libalchemy@CORE@.la

libalchemy@CORE@_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libalchemy@CORE@_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/copperplate/libcopperplate@CORE@.la

libalchemy@CORE@_la_SOURCES =	\
	init.c		\
	internal.c	\
	internal.h	\
	reference.h	\
	alarm.c		\
	alarm.h		\
	buffer.c	\
	buffer.h	\
	cond.c		\
	cond.h		\
	event.c		\
	event.h		\
	heap.c		\
	heap.h		\
	mutex.c		\
	mutex.h		\
	queue.c		\
	queue.h		\
	task.c		\
	task.h		\
	sem.c		\
	sem.h		\
	timer.c		\
	timer.h

if XENO_COBALT
libalchemy@CORE@_la_SOURCES +=	\
	pipe.c			\
	pipe.h
endif

libalchemy@CORE@_la_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include				\
	-I$(top_srcdir)/lib

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libalchemy@CORE@_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
