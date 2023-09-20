testdir = @XENO_TEST_DIR@
noinst_LIBRARIES = libalchemytests.a

libalchemytests_a_SOURCES = alchemytests.c
libalchemytests_a_CPPFLAGS =	\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/include \
	-DXENO_TEST_DIR='"$(XENO_TEST_DIR)"'

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

test_PROGRAMS =				\
		alchemytests_alarm1	\
		alchemytests_buffer1	\
		alchemytests_event1	\
		alchemytests_heap1	\
		alchemytests_heap2	\
		alchemytests_mq1	\
		alchemytests_mq2	\
		alchemytests_mq3	\
		alchemytests_mutex1	\
		alchemytests_pipe1	\
		alchemytests_sem1	\
		alchemytests_sem2	\
		alchemytests_task1	\
		alchemytests_task2	\
		alchemytests_task3	\
		alchemytests_task4	\
		alchemytests_task5	\
		alchemytests_task6	\
		alchemytests_task7	\
		alchemytests_task8	\
		alchemytests_task9	\
		alchemytests_task10

alchemycppflags =			\
	$(XENO_USER_CFLAGS)		\
	-I$(top_srcdir)/include

alchemyldadd = 							\
	../../../lib/alchemy/libalchemy@CORE@.la		\
	../../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@ 					\
	@XENO_USER_LDADD@					\
	-lpthread -lrt -lm

alchemytests_alarm1_SOURCES = alarm-1.c
alchemytests_alarm1_CPPFLAGS = $(alchemycppflags)
alchemytests_alarm1_LDADD = $(alchemyldadd)
alchemytests_alarm1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_event1_SOURCES = event-1.c
alchemytests_event1_CPPFLAGS = $(alchemycppflags)
alchemytests_event1_LDADD = $(alchemyldadd)
alchemytests_event1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_heap1_SOURCES = heap-1.c
alchemytests_heap1_CPPFLAGS = $(alchemycppflags)
alchemytests_heap1_LDADD = $(alchemyldadd)
alchemytests_heap1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_heap2_SOURCES = heap-2.c
alchemytests_heap2_CPPFLAGS = $(alchemycppflags)
alchemytests_heap2_LDADD = $(alchemyldadd)
alchemytests_heap2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_buffer1_SOURCES = buffer-1.c
alchemytests_buffer1_CPPFLAGS = $(alchemycppflags)
alchemytests_buffer1_LDADD = $(alchemyldadd)
alchemytests_buffer1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_mutex1_SOURCES = mutex-1.c
alchemytests_mutex1_CPPFLAGS = $(alchemycppflags)
alchemytests_mutex1_LDADD = $(alchemyldadd)
alchemytests_mutex1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_pipe1_SOURCES = pipe-1.c
alchemytests_pipe1_CPPFLAGS = $(alchemycppflags)
alchemytests_pipe1_LDADD = $(alchemyldadd)
alchemytests_pipe1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_mq1_SOURCES = mq-1.c
alchemytests_mq1_CPPFLAGS = $(alchemycppflags)
alchemytests_mq1_LDADD = $(alchemyldadd)
alchemytests_mq1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_mq2_SOURCES = mq-2.c
alchemytests_mq2_CPPFLAGS = $(alchemycppflags)
alchemytests_mq2_LDADD = $(alchemyldadd)
alchemytests_mq2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_mq3_SOURCES = mq-3.c
alchemytests_mq3_CPPFLAGS = $(alchemycppflags)
alchemytests_mq3_LDADD = $(alchemyldadd)
alchemytests_mq3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_sem1_SOURCES = sem-1.c
alchemytests_sem1_CPPFLAGS = $(alchemycppflags)
alchemytests_sem1_LDADD = $(alchemyldadd)
alchemytests_sem1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_sem2_SOURCES = sem-2.c
alchemytests_sem2_CPPFLAGS = $(alchemycppflags)
alchemytests_sem2_LDADD = $(alchemyldadd)
alchemytests_sem2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task1_SOURCES = task-1.c
alchemytests_task1_CPPFLAGS = $(alchemycppflags)
alchemytests_task1_LDADD = $(alchemyldadd)
alchemytests_task1_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task2_SOURCES = task-2.c
alchemytests_task2_CPPFLAGS = $(alchemycppflags)
alchemytests_task2_LDADD = $(alchemyldadd)
alchemytests_task2_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task3_SOURCES = task-3.c
alchemytests_task3_CPPFLAGS = $(alchemycppflags)
alchemytests_task3_LDADD = $(alchemyldadd)
alchemytests_task3_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task4_SOURCES = task-4.c
alchemytests_task4_CPPFLAGS = $(alchemycppflags)
alchemytests_task4_LDADD = $(alchemyldadd)
alchemytests_task4_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task5_SOURCES = task-5.c
alchemytests_task5_CPPFLAGS = $(alchemycppflags)
alchemytests_task5_LDADD = $(alchemyldadd)
alchemytests_task5_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task6_SOURCES = task-6.c
alchemytests_task6_CPPFLAGS = $(alchemycppflags)
alchemytests_task6_LDADD = $(alchemyldadd)
alchemytests_task6_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task7_SOURCES = task-7.c
alchemytests_task7_CPPFLAGS = $(alchemycppflags)
alchemytests_task7_LDADD = $(alchemyldadd)
alchemytests_task7_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task8_SOURCES = task-8.c
alchemytests_task8_CPPFLAGS = $(alchemycppflags)
alchemytests_task8_LDADD = $(alchemyldadd)
alchemytests_task8_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task9_SOURCES = task-9.c
alchemytests_task9_CPPFLAGS = $(alchemycppflags)
alchemytests_task9_LDADD = $(alchemyldadd)
alchemytests_task9_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
alchemytests_task10_SOURCES = task-10.c
alchemytests_task10_CPPFLAGS = $(alchemycppflags)
alchemytests_task10_LDADD = $(alchemyldadd)
alchemytests_task10_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@
