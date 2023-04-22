
lib_LTLIBRARIES = libcopperplate@CORE@.la

libcopperplate@CORE@_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -lpthread -lrt -version-info 0:0:0
libcopperplate@CORE@_la_LIBADD = @XENO_CORE_LDADD@

noinst_LTLIBRARIES =

libcopperplate@CORE@_la_SOURCES =	\
	clockobj.c	\
	cluster.c	\
	eventobj.c 	\
	init.c		\
	internal.c	\
	internal.h	\
	syncobj.c	\
	semobj.c	\
	threadobj.c	\
	timerobj.c	\
	traceobj.c

libcopperplate@CORE@_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

if XENO_REGISTRY
libcopperplate@CORE@_la_LIBADD += libregistry.la
noinst_LTLIBRARIES += libregistry.la

libregistry_la_SOURCES = registry.c

libregistry_la_CPPFLAGS =		\
	$(libcopperplate@CORE@_la_CPPFLAGS)	\
	@XENO_FUSE_CFLAGS@

libregistry_la_LIBADD =			\
	@FUSE_LIBS@
endif

if XENO_PSHARED
# The process shareable heap has real-time properties, therefore it
# fits both the cobalt and mercury cores equally. Yummie.
libcopperplate@CORE@_la_SOURCES += heapobj-pshared.c reference.c
endif
if XENO_TLSF
libcopperplate@CORE@_la_SOURCES += heapobj-tlsf.c
else
if XENO_HEAPMEM
libcopperplate@CORE@_la_SOURCES += heapobj-heapmem.c
else
libcopperplate@CORE@_la_SOURCES += heapobj-malloc.c
endif
endif

AM_LIBTOOLFLAGS = --silent

SUBDIRS = .

if XENO_REGISTRY
SUBDIRS += regd
endif

DIST_SUBDIRS = regd

SPARSE = sparse

sparse:
	@for i in $(libcopperplate@CORE@_la_SOURCES) ; do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
