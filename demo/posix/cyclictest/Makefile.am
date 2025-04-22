demodir = @XENO_DEMO_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

VERSION_STRING = 0.92

demo_PROGRAMS = cyclictest

cyclictest_CPPFLAGS = 				\
	$(XENO_USER_CFLAGS)			\
	-I$(top_srcdir)/include			\
	-DVERSION_STRING=$(VERSION_STRING)	\
	-Wno-strict-prototypes			\
	-Wno-implicit-function-declaration	\
	-Wno-missing-prototypes			\
	-Wno-nonnull				\
	-Wno-unused-function

cyclictest_SOURCES =	\
	cyclictest.c	\
	error.c		\
	error.h		\
	rt_numa.h	\
	rt-utils.c	\
	rt-utils.h

cyclictest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

cyclictest_LDADD =		\
	@XENO_CORE_LDADD@	\
	@XENO_USER_LDADD@ 	\
	-lpthread -lrt -lm
