
noinst_LIBRARIES = libposix-fork.a

libposix_fork_a_SOURCES = posix-fork.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libposix_fork_a_CPPFLAGS =			\
	@XENO_USER_CFLAGS@			\
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'	\
	-I$(top_srcdir)/include
