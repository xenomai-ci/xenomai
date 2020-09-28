testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = gpiotest

gpiotest_SOURCES = gpiotest.c

gpiotest_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

gpiotest_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

gpiotest_LDADD =			\
	../../lib/smokey/libsmokey@CORE@.la	\
	@XENO_CORE_LDADD@		\
	@XENO_USER_LDADD@		\
	-lpthread -lrt
