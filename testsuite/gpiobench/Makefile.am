testdir = @XENO_TEST_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS = gpiobench

gpiobench_SOURCES = gpiobench.c

gpiobench_CPPFLAGS = 		\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

gpiobench_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

gpiobench_LDADD =			\
	@XENO_CORE_LDADD@	\
	@XENO_USER_LDADD@	\
	-lpthread -lrt -lm
