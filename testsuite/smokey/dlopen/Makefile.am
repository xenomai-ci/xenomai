testdir = @XENO_TEST_DIR@

test_LTLIBRARIES = libalchemy-test.la libposix-test.la

libalchemy_test_la_LDFLAGS =	\
	$(XENO_POSIX_WRAPPERS)	\
	@XENO_LIB_LDFLAGS@	\
	-version-info 0:0:0

libalchemy_test_la_LIBADD =					\
	@XENO_CORE_LDADD@					\
	@XENO_USER_LDADD@					\
	../../../lib/boilerplate/init/libbootstrap-pic.la	\
	../../../lib/alchemy/libalchemy@CORE@.la			\
	../../../lib/copperplate/libcopperplate@CORE@.la

libalchemy_test_la_SOURCES = libalchemy-test.c

libalchemy_test_la_CPPFLAGS =	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include

libposix_test_la_LDFLAGS =	\
	$(XENO_POSIX_WRAPPERS)	\
	@XENO_LIB_LDFLAGS@	\
	-version-info 0:0:0

libposix_test_la_LIBADD =					\
	@XENO_CORE_LDADD@					\
	@XENO_USER_LDADD@					\
	../../../lib/boilerplate/init/libbootstrap-pic.la	\
	../../../lib/copperplate/libcopperplate@CORE@.la

libposix_test_la_SOURCES = libposix-test.c

libposix_test_la_CPPFLAGS =	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)/include

test_PROGRAMS = dlopentest

dlopentest_SOURCES = dlopentest.c

dlopentest_CPPFLAGS =				\
	-Wno-format-security			\
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'

dlopentest_LDADD =		\
	-ldl

noinst_LIBRARIES = libdlopen.a

libdlopen_a_SOURCES = dlopen.c

libdlopen_a_CPPFLAGS =				\
	@XENO_USER_CFLAGS@			\
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'	\
	-I$(top_srcdir)/include
