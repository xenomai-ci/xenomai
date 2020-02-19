lib_LTLIBRARIES = libvxworks.la

libvxworks_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 0:0:0

libvxworks_la_LIBADD =						\
	@XENO_CORE_LDADD@					\
	$(top_builddir)/lib/copperplate/libcopperplate.la

libvxworks_la_SOURCES = \
	init.c		\
	errnoLib.c	\
	intLib.c	\
	kernLib.c	\
	lstLib.c	\
	memPartLib.c	\
	memPartLib.h	\
	msgQLib.c	\
	msgQLib.h	\
	rngLib.c	\
	rngLib.h	\
	semLib.c	\
	semLib.h	\
	taskLib.c	\
	taskLib.h	\
	taskHookLib.c	\
	taskHookLib.h	\
	taskInfo.c	\
	tickLib.c	\
	tickLib.h	\
	wdLib.c		\
	wdLib.h		\
	sysLib.c	\
	reference.h

libvxworks_la_CPPFLAGS =				\
	@XENO_USER_CFLAGS@				\
	-I$(top_srcdir)/include				\
	-I$(top_srcdir)/lib

EXTRA_DIST = testsuite

SPARSE = sparse

sparse:
	@for i in $(libvxworks_la_SOURCES); do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
