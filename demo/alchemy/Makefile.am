demodir = @XENO_DEMO_DIR@

demo_PROGRAMS = altency

if XENO_COBALT
SUBDIRS = cobalt
endif

cppflags = 				\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

ldadd = 					\
	../../lib/alchemy/libalchemy@CORE@.la		\
	../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@ 			\
	@XENO_USER_LDADD@			\
	-lpthread -lrt -lm

altency_SOURCES = altency.c
altency_CPPFLAGS = $(cppflags)
altency_LDADD = $(ldadd) -lpthread -lrt -lm
altency_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

# This demo mixes the Alchemy and Xenomai-enabled POSIX APIs over
# Cobalt, so we ask for both set of flags. --posix along with
# --ldflags will get us the linker switches causing the symbol
# wrapping for open/read/write/ioctl and friends.  Over Mercury,
# --posix is ignored since it's implicitly enabled.
#
# CFLAGS =  $(shell xeno-config --alchemy --posix --cflags)
# LDFLAGS = $(shell xeno-config --alchemy --posix --ldflags)
