testdir = @XENO_TEST_DIR@
noinst_LIBRARIES = libvxworkstests.a

libvxworkstests_a_SOURCES = vxworkstests.c
libvxworkstests_a_CPPFLAGS =	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/include \
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS =				\
		vxworkstests		\
		vxworkstests_lst1	\
		vxworkstests_msgQ1	\
		vxworkstests_msgQ2	\
		vxworkstests_msgQ3	\
		vxworkstests_rng1	\
		vxworkstests_sem1	\
		vxworkstests_sem2	\
		vxworkstests_sem3	\
		vxworkstests_sem4	\
		vxworkstests_wd1	\
		vxworkstests_task1	\
		vxworkstests_task2

vxworkscppflags = 			\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include

vxworksldadd = 							\
	../../../lib/vxworks/libvxworks@CORE@.la		\
	../../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@ 					\
	@XENO_USER_LDADD@					\
	-lpthread -lrt -lm

vxworkstests_SOURCES = vxworkstests_driver.c
vxworkstests_CPPFLAGS = $(vxworkscppflags)
vxworkstests_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)
vxworkstests_LDADD =				\
	../../../lib/smokey/libsmokey@CORE@.la	\
	@XENO_CORE_LDADD@ 			\
	@XENO_USER_LDADD@			\
	-lpthread -lrt

vxworkstests_lst1_SOURCES = lst-1.c
vxworkstests_lst1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_lst1_LDADD = $(vxworksldadd)
vxworkstests_lst1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_task1_SOURCES = task-1.c
vxworkstests_task1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_task1_LDADD = $(vxworksldadd)
vxworkstests_task1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_task2_SOURCES = task-2.c
vxworkstests_task2_CPPFLAGS = $(vxworkscppflags)
vxworkstests_task2_LDADD = $(vxworksldadd)
vxworkstests_task2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_msgQ1_SOURCES = msgQ-1.c
vxworkstests_msgQ1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_msgQ1_LDADD = $(vxworksldadd)
vxworkstests_msgQ1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_msgQ2_SOURCES = msgQ-2.c
vxworkstests_msgQ2_CPPFLAGS = $(vxworkscppflags)
vxworkstests_msgQ2_LDADD = $(vxworksldadd)
vxworkstests_msgQ2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_msgQ3_SOURCES = msgQ-3.c
vxworkstests_msgQ3_CPPFLAGS = $(vxworkscppflags)
vxworkstests_msgQ3_LDADD = $(vxworksldadd)
vxworkstests_msgQ3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_rng1_SOURCES = rng-1.c
vxworkstests_rng1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_rng1_LDADD = $(vxworksldadd)
vxworkstests_rng1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_sem1_SOURCES = sem-1.c
vxworkstests_sem1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_sem1_LDADD = $(vxworksldadd)
vxworkstests_sem1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_sem2_SOURCES = sem-2.c
vxworkstests_sem2_CPPFLAGS = $(vxworkscppflags)
vxworkstests_sem2_LDADD = $(vxworksldadd)
vxworkstests_sem2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_sem3_SOURCES = sem-3.c
vxworkstests_sem3_CPPFLAGS = $(vxworkscppflags)
vxworkstests_sem3_LDADD = $(vxworksldadd)
vxworkstests_sem3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_sem4_SOURCES = sem-4.c
vxworkstests_sem4_CPPFLAGS = $(vxworkscppflags)
vxworkstests_sem4_LDADD = $(vxworksldadd)
vxworkstests_sem4_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
vxworkstests_wd1_SOURCES = wd-1.c
vxworkstests_wd1_CPPFLAGS = $(vxworkscppflags)
vxworkstests_wd1_LDADD = $(vxworksldadd)
vxworkstests_wd1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
