testdir = @XENO_TEST_DIR@
noinst_LIBRARIES = libpsostests.a

libpsostests_a_SOURCES = psostests.c
libpsostests_a_CPPFLAGS = 	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/include \
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS =			\
		psostests       \
		psostests_mq1	\
		psostests_mq2	\
		psostests_mq3	\
		psostests_pt1	\
		psostests_rn1	\
		psostests_sem1	\
		psostests_sem2	\
		psostests_tm1	\
		psostests_tm2	\
		psostests_tm3	\
		psostests_tm4	\
		psostests_tm5	\
		psostests_tm6	\
		psostests_tm7	\
		psostests_task1	\
		psostests_task2	\
		psostests_task3	\
		psostests_task4	\
		psostests_task5	\
		psostests_task6	\
		psostests_task7	\
		psostests_task8	\
		psostests_task9

psoscppflags =				\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)			\
	-I$(top_srcdir)/include

psosldadd =							\
	../../../lib/psos/libpsos@CORE@.la			\
	../../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@ 					\
	@XENO_USER_LDADD@					\
	-lpthread -lrt -lm

psostests_SOURCES = psostests_driver.c
psostests_CPPFLAGS = $(psoscppflags)
psostests_LDADD =						\
	../../../lib/smokey/libsmokey@CORE@.la			\
	@XENO_CORE_LDADD@ 					\
	@XENO_USER_LDADD@					\
	-lpthread -lrt
psostests_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@ $(XENO_POSIX_WRAPPERS)

psostests_mq1_SOURCES = mq-1.c
psostests_mq1_CPPFLAGS = $(psoscppflags)
psostests_mq1_LDADD = $(psosldadd)
psostests_mq1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_mq2_SOURCES = mq-2.c
psostests_mq2_CPPFLAGS = $(psoscppflags)
psostests_mq2_LDADD = $(psosldadd)
psostests_mq2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_mq3_SOURCES = mq-3.c
psostests_mq3_CPPFLAGS = $(psoscppflags)
psostests_mq3_LDADD = $(psosldadd)
psostests_mq3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_pt1_SOURCES = pt-1.c
psostests_pt1_CPPFLAGS = $(psoscppflags)
psostests_pt1_LDADD = $(psosldadd)
psostests_pt1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_rn1_SOURCES = rn-1.c
psostests_rn1_CPPFLAGS = $(psoscppflags)
psostests_rn1_LDADD = $(psosldadd)
psostests_rn1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_sem1_SOURCES = sem-1.c
psostests_sem1_CPPFLAGS = $(psoscppflags)
psostests_sem1_LDADD = $(psosldadd)
psostests_sem1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_sem2_SOURCES = sem-2.c
psostests_sem2_CPPFLAGS = $(psoscppflags)
psostests_sem2_LDADD = $(psosldadd)
psostests_sem2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm1_SOURCES = tm-1.c
psostests_tm1_CPPFLAGS = $(psoscppflags)
psostests_tm1_LDADD = $(psosldadd)
psostests_tm1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm2_SOURCES = tm-2.c
psostests_tm2_CPPFLAGS = $(psoscppflags)
psostests_tm2_LDADD = $(psosldadd)
psostests_tm2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm3_SOURCES = tm-3.c
psostests_tm3_CPPFLAGS = $(psoscppflags)
psostests_tm3_LDADD = $(psosldadd)
psostests_tm3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm4_SOURCES = tm-4.c
psostests_tm4_CPPFLAGS = $(psoscppflags)
psostests_tm4_LDADD = $(psosldadd)
psostests_tm4_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm5_SOURCES = tm-5.c
psostests_tm5_CPPFLAGS = $(psoscppflags)
psostests_tm5_LDADD = $(psosldadd)
psostests_tm5_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm6_SOURCES = tm-6.c
psostests_tm6_CPPFLAGS = $(psoscppflags)
psostests_tm6_LDADD = $(psosldadd)
psostests_tm6_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_tm7_SOURCES = tm-7.c
psostests_tm7_CPPFLAGS = $(psoscppflags)
psostests_tm7_LDADD = $(psosldadd)
psostests_tm7_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task1_SOURCES = task-1.c
psostests_task1_CPPFLAGS = $(psoscppflags)
psostests_task1_LDADD = $(psosldadd)
psostests_task1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task2_SOURCES = task-2.c
psostests_task2_CPPFLAGS = $(psoscppflags)
psostests_task2_LDADD = $(psosldadd)
psostests_task2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task3_SOURCES = task-3.c
psostests_task3_CPPFLAGS = $(psoscppflags)
psostests_task3_LDADD = $(psosldadd)
psostests_task3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task4_SOURCES = task-4.c
psostests_task4_CPPFLAGS = $(psoscppflags)
psostests_task4_LDADD = $(psosldadd)
psostests_task4_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task5_SOURCES = task-5.c
psostests_task5_CPPFLAGS = $(psoscppflags)
psostests_task5_LDADD = $(psosldadd)
psostests_task5_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task6_SOURCES = task-6.c
psostests_task6_CPPFLAGS = $(psoscppflags)
psostests_task6_LDADD = $(psosldadd)
psostests_task6_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task7_SOURCES = task-7.c
psostests_task7_CPPFLAGS = $(psoscppflags)
psostests_task7_LDADD = $(psosldadd)
psostests_task7_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task8_SOURCES = task-8.c
psostests_task8_CPPFLAGS = $(psoscppflags)
psostests_task8_LDADD = $(psosldadd)
psostests_task8_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
psostests_task9_SOURCES = task-9.c
psostests_task9_CPPFLAGS = $(psoscppflags)
psostests_task9_LDADD = $(psosldadd)
psostests_task9_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
