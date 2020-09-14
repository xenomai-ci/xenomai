
lib_LTLIBRARIES = libcopperplate.la

libcopperplate_la_LDFLAGS = @XENO_LIB_LDFLAGS@ -lpthread -lrt -version-info 0:0:0
libcopperplate_la_LIBADD = @XENO_CORE_LDADD@

noinst_LTLIBRARIES =

libcopperplate_la_SOURCES =	\
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

libcopperplate_la_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

if XENO_REGISTRY
libcopperplate_la_LIBADD += libregistry.la
noinst_LTLIBRARIES += libregistry.la

libregistry_la_SOURCES = registry.c

libregistry_la_CPPFLAGS =		\
	$(libcopperplate_la_CPPFLAGS)	\
	@XENO_FUSE_CFLAGS@
endif

if XENO_PSHARED
# The process shareable heap has real-time properties, therefore it
# fits both the cobalt and mercury cores equally. Yummie.
libcopperplate_la_SOURCES += heapobj-pshared.c reference.c
endif
if XENO_TLSF
libcopperplate_la_SOURCES += heapobj-tlsf.c
else
if XENO_HEAPMEM
libcopperplate_la_SOURCES += heapobj-heapmem.c
else
libcopperplate_la_SOURCES += heapobj-malloc.c
endif
endif

SUBDIRS = .

if XENO_REGISTRY
SUBDIRS += regd
endif

DIST_SUBDIRS = regd

SPARSE = sparse

sparse:
	@for i in $(libcopperplate_la_SOURCES) ; do \
		$(SPARSE) $(CHECKFLAGS) $(srcdir)/$$i; \
	done
