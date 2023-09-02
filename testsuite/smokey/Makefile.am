testdir = @XENO_TEST_DIR@
test_PROGRAMS = smokey

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

smokey_SOURCES = main.c

# Make sure to list modules from the most dependent to the
# least. e.g. net_common should appear after all net_* modules,
# memcheck should appear after all heapmem-* modules.

COBALT_SUBDIRS = 	\
	arith 		\
	bufp		\
	can		\
	cpu-affinity	\
	fpu-stress	\
	gdb		\
	iddp		\
	leaks		\
	memory-coreheap	\
	memory-heapmem	\
	memory-tlsf	\
	memcheck	\
	net_packet_dgram\
	net_packet_raw	\
	net_udp		\
	net_common	\
	posix-clock	\
	posix-cond 	\
	posix-fork	\
	posix-mutex 	\
	posix-select 	\
	rtdm 		\
	sched-quota 	\
	sched-tp 	\
	setsched	\
	sigdebug	\
	timerfd		\
	tsc		\
	xddp		\
	y2038		\
	alchemytests	\
	vxworkstests	\
	psostests

MERCURY_SUBDIRS =	\
	memory-heapmem	\
	memory-tlsf	\
	memcheck	\
	alchemytests	\
	vxworkstests	\
	psostests

DIST_SUBDIRS = 		\
	arith 		\
	bufp		\
	can		\
	cpu-affinity	\
	dlopen		\
	fpu-stress	\
	gdb		\
	iddp		\
	leaks		\
	memory-coreheap	\
	memory-heapmem	\
	memory-pshared	\
	memory-tlsf	\
	memcheck	\
	net_packet_dgram\
	net_packet_raw	\
	net_udp		\
	net_common	\
	posix-clock	\
	posix-cond 	\
	posix-fork	\
	posix-mutex 	\
	posix-select 	\
	rtdm 		\
	sched-quota 	\
	sched-tp 	\
	setsched	\
	sigdebug	\
	timerfd		\
	tsc		\
	xddp		\
	y2038		\
	alchemytests	\
	vxworkstests	\
	psostests

if XENO_X86
DIST_SUBDIRS += x86io
endif

if XENO_COBALT
if CONFIG_XENO_LIBS_DLOPEN
COBALT_SUBDIRS += dlopen
endif
if XENO_PSHARED
COBALT_SUBDIRS += memory-pshared
endif
if XENO_X86
COBALT_SUBDIRS += x86io
endif
wrappers = $(XENO_POSIX_WRAPPERS)
SUBDIRS = $(COBALT_SUBDIRS)
else
if XENO_PSHARED
MERCURY_SUBDIRS += memory-pshared
endif
SUBDIRS = $(MERCURY_SUBDIRS)
wrappers =
endif

plugin_list = $(foreach plugin,$(SUBDIRS),$(plugin)/lib$(plugin).a)
# wrap-link.sh is confused by -whole-archive, so work around
# this by forcing undefined references to symbols we expect the
# plugins to export.
sym_prefix=@XENO_SYMBOL_PREFIX@
undef_list = $(foreach plugin,$(SUBDIRS),-u $(sym_prefix)smokey_plugin_$(subst -,_,$(plugin)))

smokey_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

smokey_LDFLAGS=$(wrappers) @XENO_AUTOINIT_LDFLAGS@ $(undef_list)

smokey_LDADD = 					\
	$(plugin_list)				\
	../../lib/smokey/libsmokey@CORE@.la		\
	../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@			\
	 @XENO_USER_LDADD@			\
	-lpthread -lrt
