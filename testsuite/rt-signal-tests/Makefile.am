testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = rt_signal_test_fpe rt_signal_test_segfault rt_signal_test_ill

alchemycppflags =              			 \
	$(XENO_USER_CFLAGS)    			 \
	-I$(top_srcdir)/include

alchemyldadd =					\
	@XENO_AUTOINIT_LDFLAGS@			\
	$(XENO_POSIX_WRAPPERS)			\
	../../lib/alchemy/libalchemy.la		\
	../../lib/copperplate/libcopperplate.la	\
	@XENO_CORE_LDADD@			\
	@XENO_USER_LDADD@			\
	-lrt -lpthread -lm

rt_signal_test_fpe_SOURCES = rt_signal_test.c
rt_signal_test_fpe_CPPFLAGS = $(alchemycppflags) -DTEST_FPE=1
rt_signal_test_fpe_LDADD = $(alchemyldadd)

rt_signal_test_segfault_SOURCES = rt_signal_test.c
rt_signal_test_segfault_CPPFLAGS = $(alchemycppflags) -DTEST_SEGFAULT=1
rt_signal_test_segfault_LDADD = $(alchemyldadd)

rt_signal_test_ill_SOURCES = rt_signal_test.c
rt_signal_test_ill_CPPFLAGS = $(alchemycppflags)
rt_signal_test_ill_LDADD = $(alchemyldadd)

