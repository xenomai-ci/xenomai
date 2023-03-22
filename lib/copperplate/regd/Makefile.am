sbin_PROGRAMS = sysregd

AM_CPPFLAGS = 			\
	@XENO_USER_CFLAGS@	\
	@XENO_FUSE_CFLAGS@	\
	-I$(top_srcdir)/lib	\
	-I$(top_srcdir)/include

AM_LDFLAGS = $(XENO_POSIX_WRAPPERS)

LDADD = 			\
	../libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@	\
	@XENO_USER_LDADD@	\
	-lpthread -lrt

sysregd_SOURCES = regd.c fs-common.c sysregfs.h

if XENO_COBALT
sysregd_SOURCES += fs-cobalt.c
else
sysregd_SOURCES += fs-mercury.c
endif
