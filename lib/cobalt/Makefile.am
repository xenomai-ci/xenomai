pkgconfigdir = $(libdir)/pkgconfig

noinst_HEADERS =	\
	current.h	\
	umm.h		\
	internal.h

lib_LTLIBRARIES = libcobalt.la libmodechk.la

libcobalt_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -version-info 2:0:0 -lpthread -lrt

libcobalt_la_LIBADD = \
	arch/@XENO_TARGET_ARCH@/libarch.la	\
	../boilerplate/libboilerplate.la

libcobalt_la_SOURCES =		\
	attr.c			\
	clock.c			\
	cond.c			\
	current.c		\
	init.c			\
	internal.c		\
	mq.c			\
	mutex.c			\
	parse_vdso.c		\
	printf.c		\
	rtdm.c			\
	sched.c			\
	select.c		\
	semaphore.c		\
	signal.c		\
	sigshadow.c		\
	thread.c		\
	ticks.c			\
	timer.c			\
	timerfd.c		\
	trace.c			\
	umm.c			\
	wrappers.c

libcobalt_la_CPPFLAGS =			\
	@XENO_COBALT_CFLAGS@		\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include

libmodechk_la_LIBADD = libcobalt.la

libmodechk_la_SOURCES =	\
	malloc.c	\
	malloc-nowrap.c

libmodechk_la_CPPFLAGS =		\
	@XENO_COBALT_CFLAGS@		\
	-I$(top_srcdir)/include/cobalt	\
	-I$(top_srcdir)/include

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/cobalt.wrappers $(DESTDIR)$(libdir)
	$(INSTALL_DATA) $(srcdir)/modechk.wrappers $(DESTDIR)$(libdir)

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/cobalt.wrappers
	$(RM) $(DESTDIR)$(libdir)/modechk.wrappers

EXTRA_DIST = cobalt.wrappers modechk.wrappers

SUBDIRS = arch
