/*
 * Copyright (C) 2004-2015 Philippe Gerum <rpm@xenomai.org>
 * Copyright (C) 2014 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/timerfd.h>
#include <xeno_config.h>
#include <rtdm/testing.h>
#include <boilerplate/trace.h>
#include <xenomai/init.h>

pthread_t latency_task, display_task;

sem_t *display_sem;

#define ONE_BILLION	1000000000
#define TEN_MILLIONS	10000000

#define HIPRIO 99
#define LOPRIO 0

unsigned max_relaxed;
int32_t minjitter, maxjitter, avgjitter;
int32_t gminjitter = TEN_MILLIONS, gmaxjitter = -TEN_MILLIONS, goverrun = 0;
int64_t gavgjitter = 0;

long long period_ns = 0;
int test_duration = 0;		/* sec of testing, via -T <sec>, 0 is inf */
int data_lines = 21;		/* data lines per header line, -l <lines> to change */
int quiet = 0;			/* suppress printing of RTH, RTD lines when -T given */
int benchdev = -1;
int freeze_max = 0;
int priority = HIPRIO;
int stop_upon_switch = 0;
sig_atomic_t sampling_relaxed = 0;
char sem_name[16];

#define USER_TASK       0
#define KERNEL_TASK     1
#define TIMER_HANDLER   2

int test_mode = USER_TASK;
const char *test_mode_names[] = {
	"periodic user-mode task",
	"in-kernel periodic task",
	"in-kernel timer handler"
};

time_t test_start, test_end;	/* report test duration */
int test_loops = 0;		/* outer loop count */

/* Warmup time : in order to avoid spurious cache effects on low-end machines. */
#define WARMUP_TIME 1
#define HISTOGRAM_CELLS 300
int histogram_size = HISTOGRAM_CELLS;
int32_t *histogram_avg = NULL, *histogram_max = NULL, *histogram_min = NULL;

char *do_gnuplot = NULL;
int do_histogram = 0, do_stats = 0, finished = 0;
int bucketsize = 1000;		/* default = 1000ns, -B <size> to override */
int max_latency_treshold = 0;	/* default = 0 (diabled), -m <max> (in us) to override */

#define need_histo() (do_histogram || do_stats || do_gnuplot)

static inline void add_histogram(int32_t *histogram, int32_t addval)
{
	/* bucketsize steps */
	int inabs = (addval >= 0 ? addval : -addval) / bucketsize;
	histogram[inabs < histogram_size ? inabs : histogram_size - 1]++;
}

static inline long long diff_ts(struct timespec *left, struct timespec *right)
{
	return (long long)(left->tv_sec - right->tv_sec) * ONE_BILLION
		+ left->tv_nsec - right->tv_nsec;
}

static void *latency(void *cookie)
{
	int err, count, nsamples, warmup = 1;
	unsigned long long fault_threshold;
	struct itimerspec timer_conf;
	struct timespec expected;
	unsigned old_relaxed = 0;
	char task_name[16];
	int tfd;

	snprintf(task_name, sizeof(task_name), "sampling-%d", getpid());
	err = pthread_setname_np(pthread_self(), task_name);
	if (err)
		error(1, err, "pthread_setname_np(latency)");

	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (tfd == -1)
		error(1, errno, "timerfd_create()");

#ifdef CONFIG_XENO_COBALT
	err = pthread_setmode_np(0, PTHREAD_WARNSW, NULL);
	if (err)
		error(1, err, "pthread_setmode_np()");
#endif

	err = clock_gettime(CLOCK_MONOTONIC, &expected);
	if (err)
		error(1, errno, "clock_gettime()");

	fault_threshold = CONFIG_XENO_DEFAULT_PERIOD;
	nsamples = (long long)ONE_BILLION / period_ns;
	/* start time: one millisecond from now. */
	expected.tv_nsec += 1000000;
	if (expected.tv_nsec > ONE_BILLION) {
		expected.tv_nsec -= ONE_BILLION;
		expected.tv_sec++;
	}
	timer_conf.it_value = expected;
	timer_conf.it_interval.tv_sec = period_ns / ONE_BILLION;
	timer_conf.it_interval.tv_nsec = period_ns % ONE_BILLION;

	err = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &timer_conf, NULL);
	if (err)
		error(1, errno, "timerfd_settime()");

	for (;;) {
		int32_t minj = TEN_MILLIONS, maxj = -TEN_MILLIONS, dt;
		uint32_t overrun = 0;
		int64_t sumj;

		test_loops++;

		for (count = sumj = 0; count < nsamples; count++) {
			unsigned int new_relaxed;
			struct timespec now;
			uint64_t ticks;

			err = read(tfd, &ticks, sizeof(ticks));

			clock_gettime(CLOCK_MONOTONIC, &now);
			dt = (int32_t)diff_ts(&now, &expected);
			new_relaxed = sampling_relaxed;
			if (dt > maxj) {
				if (new_relaxed != old_relaxed
				    && dt > fault_threshold)
					max_relaxed +=
						new_relaxed - old_relaxed;
				maxj = dt;
			}
			old_relaxed = new_relaxed;
			if (dt < minj)
				minj = dt;
			sumj += dt;

			if (err < 0)
				error(1, errno, "read()");
			if (ticks > 1)
				overrun += ticks - 1;
			expected.tv_nsec += (ticks * period_ns) % ONE_BILLION;
			expected.tv_sec += (ticks * period_ns) / ONE_BILLION;
			if (expected.tv_nsec > ONE_BILLION) {
				expected.tv_nsec -= ONE_BILLION;
				expected.tv_sec++;
			}

			if (freeze_max && (dt > gmaxjitter)
			    && !(finished || warmup)) {
				xntrace_latpeak_freeze(dt);
				gmaxjitter = dt;
			}

			if (!(finished || warmup) && need_histo())
				add_histogram(histogram_avg, dt);
		}

		if (!warmup) {
			if (!finished && need_histo()) {
				add_histogram(histogram_max, maxj);
				add_histogram(histogram_min, minj);
			}

			minjitter = minj;
			if (minj < gminjitter)
				gminjitter = minj;

			maxjitter = maxj;
			if (maxj > gmaxjitter)
				gmaxjitter = maxj;

			avgjitter = sumj / nsamples;
			gavgjitter += avgjitter;
			goverrun += overrun;
			sem_post(display_sem);
		}

		if (warmup && test_loops == WARMUP_TIME) {
			test_loops = 0;
			warmup = 0;
		}
	}

	return NULL;
}

static void *display(void *cookie)
{
	char task_name[16];
	int err, n = 0;
	time_t start;

	snprintf(task_name, sizeof(task_name), "display-%d", getpid());
	err = pthread_setname_np(pthread_self(), task_name);
	if (err)
		error(1, err, "pthread_setname_np(display)");

	if (test_mode == USER_TASK) {
		snprintf(sem_name, sizeof(sem_name), "/dispsem-%d", getpid());
		sem_unlink(sem_name); /* may fail */
		display_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 0);
		if (display_sem == SEM_FAILED)
			error(1, errno, "sem_open()");
	} else {
		struct rttst_tmbench_config config;

		if (test_mode == KERNEL_TASK)
			config.mode = RTTST_TMBENCH_TASK;
		else
			config.mode = RTTST_TMBENCH_HANDLER;

		config.period = period_ns;
		config.priority = priority;
		config.warmup_loops = WARMUP_TIME;
		config.histogram_size = need_histo() ? histogram_size : 0;
		config.histogram_bucketsize = bucketsize;
		config.freeze_max = freeze_max;

		err = ioctl(benchdev, RTTST_RTIOC_TMBENCH_START, &config);
		if (err)
			error(1, errno, "ioctl(RTTST_RTIOC_TMBENCH_START)");
	}

	time(&start);

	if (WARMUP_TIME)
		printf("warming up...\n");

	if (quiet)
		fprintf(stderr, "running quietly for %d seconds\n",
			test_duration);

	for (;;) {
		long minj, gminj, maxj, gmaxj, avgj;

		if (test_mode == USER_TASK) {
			err = sem_wait(display_sem);

			if (err < 0) {
				if (errno != EIDRM)
					error(1, errno, "sem_wait()");

				return NULL;
			}

			/* convert jitters to nanoseconds. */
			minj = minjitter;
			gminj = gminjitter;
			avgj = avgjitter;
			maxj = maxjitter;
			gmaxj = gmaxjitter;

		} else {
			struct rttst_interm_bench_res result;

			err = ioctl(benchdev, RTTST_RTIOC_INTERM_BENCH_RES,
				&result);

			if (err < 0) {
				if (errno != EIDRM)
					error(1, errno,
					      "ioctl(RTTST_RTIOC_INTERM_BENCH_RES)");

				return NULL;
			}

			minj = result.last.min;
			gminj = result.overall.min;
			avgj = result.last.avg;
			maxj = result.last.max;
			gmaxj = result.overall.max;
			goverrun = result.overall.overruns;
		}

		if (!quiet) {
			if (data_lines && (n++ % data_lines) == 0) {
				time_t now, dt;
				time(&now);
				dt = now - start - WARMUP_TIME;
				printf("RTT|  %.2lld:%.2lld:%.2lld  (%s, %lld us period, priority %d)\n",
				       (long long)(dt / 3600),
				       (long long)((dt / 60) % 60),
				       (long long)(dt % 60),
				       test_mode_names[test_mode],
				       period_ns / 1000, priority);
				printf("RTH|%11s|%11s|%11s|%8s|%6s|%11s|%11s\n",
				       "----lat min", "----lat avg",
				       "----lat max", "-overrun", "---msw",
				       "---lat best", "--lat worst");
			}
			printf("RTD|%11.3f|%11.3f|%11.3f|%8d|%6u|%11.3f|%11.3f\n",
			       (double)minj / 1000,
			       (double)avgj / 1000,
			       (double)maxj / 1000,
			       goverrun,
			       max_relaxed,
			       (double)gminj / 1000, (double)gmaxj / 1000);
		}
	}

	return NULL;
}

static double dump_histogram(int32_t *histogram, char *kind)
{
	int n, total_hits = 0;
	double avg = 0;		/* used to sum hits 1st */

	if (do_histogram)
		printf("---|--param|----range-|--samples\n");

	for (n = 0; n < histogram_size; n++) {
		int32_t hits = histogram[n];

		if (hits) {
			total_hits += hits;
			avg += n * hits;
			if (do_histogram)
				printf("HSD|    %s| %3d -%3d | %8d\n",
				       kind, n, n + 1, hits);
		}
	}

	avg /= total_hits;	/* compute avg, reuse variable */

	return avg;
}

static void dump_histo_gnuplot(int32_t *histogram, time_t duration)
{
	unsigned int start, stop;
	char *xconf, buf[BUFSIZ];
	FILE *ifp, *ofp;
	int n;

	if (strcmp(do_gnuplot, "-") == 0)
		ofp = stdout;
	else {
		ofp = fopen(do_gnuplot, "w");
		if (ofp == NULL)
			return;
	}

	fprintf(ofp,
		"# %.2lld:%.2lld:%.2lld (%s, %lld us period, priority %d)\n",
		(long long)duration / 3600, (long long)((duration / 60) % 60),
		(long long)(duration % 60), test_mode_names[test_mode],
		period_ns / 1000, priority);
	fprintf(ofp, "# %11s|%11s|%11s|%8s|%6s|\n",
		"----lat min", "----lat avg",
		"----lat max", "-overrun", "---msw");
	fprintf(ofp,
		"# %11.3f|%11.3f|%11.3f|%8d|%6u|\n",
		(double)gminjitter / 1000, (double)gavgjitter / 1000,
		(double)gmaxjitter / 1000, goverrun, max_relaxed);

	if (asprintf(&xconf, "%s/bin/xeno-config --info", CONFIG_XENO_PREFIX) < 0)
		goto dump_data;

	ifp = popen(xconf, "r");
	free(xconf);
	if (ifp == NULL)
		goto dump_data;

	while (fgets(buf, sizeof(buf), ifp)) {
		fputc('#', ofp);
		fputc(' ', ofp);
		fputs(buf, ofp);
	}

	pclose(ifp);

dump_data:
	for (n = 0; n < histogram_size && histogram[n] == 0; n++)
		;
	start = n;

	for (n = histogram_size - 1; n >= 0 && histogram[n] == 0; n--)
		;
	stop = n;

	fprintf(ofp, "%g 1\n", start * bucketsize / 1000.0);
	for (n = start; n <= stop; n++)
		fprintf(ofp, "%g %d\n",
			(n + 0.5) * bucketsize / 1000.0, histogram[n] + 1);
	fprintf(ofp, "%g 1\n", (stop + 1) * bucketsize / 1000.0);

	if (ofp != stdout)
		fclose(ofp);
}

static void dump_stats(int32_t *histogram, char *kind, double avg)
{
	int n, total_hits = 0;
	double variance = 0;

	for (n = 0; n < histogram_size; n++) {
		int32_t hits = histogram[n];

		if (hits) {
			total_hits += hits;
			variance += hits * (n - avg) * (n - avg);
		}
	}

	/* compute std-deviation (unbiased form) */
	if (total_hits > 1) {
		variance /= total_hits - 1;
		variance = sqrt(variance);
	} else
		variance = 0;

	printf("HSS|    %s| %9d| %10.3f| %10.3f\n",
	       kind, total_hits, avg, variance);
}

static void dump_hist_stats(time_t duration)
{
	double minavg, maxavg, avgavg;

	/* max is last, where its visible w/o scrolling */
	minavg = dump_histogram(histogram_min, "min");
	avgavg = dump_histogram(histogram_avg, "avg");
	maxavg = dump_histogram(histogram_max, "max");

	printf("HSH|--param|--samples-|--average--|---stddev--\n");

	dump_stats(histogram_min, "min", minavg);
	dump_stats(histogram_avg, "avg", avgavg);
	dump_stats(histogram_max, "max", maxavg);

	if (do_gnuplot)
		dump_histo_gnuplot(histogram_avg, duration);
}

static void cleanup(void)
{
	struct rttst_overall_bench_res overall;
	time_t actual_duration;

	time(&test_end);
	actual_duration = test_end - test_start - WARMUP_TIME;
	if (!test_duration)
		test_duration = actual_duration;

	pthread_cancel(display_task);

	if (test_mode == USER_TASK) {
		pthread_cancel(latency_task);
		pthread_join(latency_task, NULL);
		pthread_join(display_task, NULL);

		sem_close(display_sem);
		sem_unlink(sem_name);
		gavgjitter /= (test_loops > 1 ? test_loops : 2) - 1;
	} else {
		overall.histogram_min = histogram_min;
		overall.histogram_max = histogram_max;
		overall.histogram_avg = histogram_avg;
		ioctl(benchdev, RTTST_RTIOC_TMBENCH_STOP, &overall);
		gminjitter = overall.result.min;
		gmaxjitter = overall.result.max;
		gavgjitter = overall.result.avg;
		goverrun = overall.result.overruns;
		pthread_join(display_task, NULL);
	}

	if (benchdev >= 0)
		close(benchdev);

	if (need_histo())
		dump_hist_stats(actual_duration);

	printf("---|-----------|-----------|-----------|--------|------|-------------------------\n"
	       "RTS|%11.3f|%11.3f|%11.3f|%8d|%6u|    %.2lld:%.2lld:%.2lld/%.2d:%.2d:%.2d\n",
	       (double)gminjitter / 1000, (double)gavgjitter / 1000,
	       (double)gmaxjitter / 1000, goverrun, max_relaxed,
	       (long long)(actual_duration / 3600),
	       (long long)((actual_duration / 60) % 60),
	       (long long)(actual_duration % 60), test_duration / 3600,
	       (test_duration / 60) % 60, test_duration % 60);
	if (max_relaxed > 0)
		printf(
"Warning! some latency peaks may have been due to involuntary mode switches.\n"
"Please contact xenomai@xenomai.org\n");

	if (histogram_avg)
		free(histogram_avg);
	if (histogram_max)
		free(histogram_max);
	if (histogram_min)
		free(histogram_min);

	if (max_latency_treshold > 0 && (double)gmaxjitter / 1000 > max_latency_treshold) {
		printf("\nMaximum latency exceeded: %11.3f > %d\n",
		       (double)gmaxjitter / 1000, max_latency_treshold);
		exit(1);
	}

	exit(0);
}

static void faulthand(int sig)
{
	xntrace_user_freeze(0, 1);
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

#ifdef CONFIG_XENO_COBALT

#include <cobalt/uapi/syscall.h>

static const char *reason_str[] = {
	[SIGDEBUG_UNDEFINED] = "received SIGDEBUG for unknown reason",
	[SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
	[SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
	[SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
	[SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
	[SIGDEBUG_NOMLOCK] = "process memory not locked",
	[SIGDEBUG_WATCHDOG] = "watchdog triggered (period too short?)",
	[SIGDEBUG_LOCK_BREAK] = "scheduler lock break",
};

static void sigdebug(int sig, siginfo_t *si, void *context)
{
	const char fmt[] = "%s, aborting.\n"
		"(enabling CONFIG_XENO_OPT_DEBUG_TRACE_RELAX may help)\n";
	unsigned int reason = sigdebug_reason(si);
	int n __attribute__ ((unused));
	static char buffer[256];

	if (reason > SIGDEBUG_WATCHDOG)
		reason = SIGDEBUG_UNDEFINED;

	switch(reason) {
	case SIGDEBUG_UNDEFINED:
	case SIGDEBUG_NOMLOCK:
	case SIGDEBUG_WATCHDOG:
		n = snprintf(buffer, sizeof(buffer), "latency: %s\n",
			     reason_str[reason]);
		n = write(STDERR_FILENO, buffer, n);
		exit(EXIT_FAILURE);
	}

	if (!stop_upon_switch) {
		++sampling_relaxed;
		return;
	}

	n = snprintf(buffer, sizeof(buffer), fmt, reason_str[reason]);
	n = write(STDERR_FILENO, buffer, n);
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

#endif /* CONFIG_XENO_COBALT */

void application_usage(void)
{
        fprintf(stderr, "usage: %s [options]:\n", get_program_name());
	fprintf(stderr,
		"-h                              print histograms of min, avg, max latencies\n"
		"-g <file>                       dump histogram to <file> in gnuplot format\n"
		"-s                              print statistics of min, avg, max latencies\n"
		"-H <histogram-size>             default = 200, increase if your last bucket is full\n"
		"-B <bucket-size>                default = 1000ns, decrease for more resolution\n"
		"-p <period_us>                  sampling period\n"
		"-l <data-lines per header>      default=21, 0 to supress headers\n"
		"-T <test_duration_seconds>      default=0, so ^C to end\n"
		"-q                              supresses RTD, RTH lines if -T is used\n"
		"-D <testing_device_no>          number of testing device, default=0\n"
		"-t <test_mode>                  0=user task (default), 1=kernel task, 2=timer IRQ\n"
		"-f                              freeze trace for each new max latency\n"
		"-c <cpu>                        pin measuring task down to given CPU\n"
		"-P <priority>                   task priority (test mode 0 and 1 only)\n"
		"-b                              break upon mode switch\n"
		"-m <max>                        fail if the maximum latency is larger than max\n"
		);
}

static void setup_sched_parameters(pthread_attr_t *attr, int prio)
{
	struct sched_param p;
	int ret;
	
	ret = pthread_attr_init(attr);
	if (ret)
		error(1, ret, "pthread_attr_init()");

	ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
	if (ret)
		error(1, ret, "pthread_attr_setinheritsched()");

	ret = pthread_attr_setschedpolicy(attr, prio ? SCHED_FIFO : SCHED_OTHER);
	if (ret)
		error(1, ret, "pthread_attr_setschedpolicy()");

	p.sched_priority = prio;
	ret = pthread_attr_setschedparam(attr, &p);
	if (ret)
		error(1, ret, "pthread_attr_setschedparam()");
}

int main(int argc, char *const *argv)
{
	struct sigaction sa __attribute__((unused));
	int c, ret, sig, cpu = 0;
	pthread_attr_t tattr;
	cpu_set_t cpus;
	sigset_t mask;

	while ((c = getopt(argc, argv, "g:hp:l:T:qH:B:sD:t:fc:P:bm:")) != EOF)
		switch (c) {
		case 'g':
			do_gnuplot = strdup(optarg);
			break;

		case 'h':

			do_histogram = 1;
			break;

		case 's':

			do_stats = 1;
			break;

		case 'H':

			histogram_size = atoi(optarg);
			break;

		case 'B':

			bucketsize = atoi(optarg);
			break;

		case 'p':

			period_ns = atoi(optarg) * 1000LL;
			if (period_ns > ONE_BILLION)
				error(1, EINVAL,
				      "period cannot be longer than 1s");
			break;

		case 'l':

			data_lines = atoi(optarg);
			break;

		case 'T':

			test_duration = atoi(optarg);
			alarm(test_duration + WARMUP_TIME);
			break;

		case 'q':

			quiet = 1;
			break;

		case 't':

			test_mode = atoi(optarg);
			break;

		case 'f':

			freeze_max = 1;
			break;

		case 'c':
			cpu = atoi(optarg);
			if (cpu < 0 || cpu >= CPU_SETSIZE)
				error(1, EINVAL, "invalid CPU #%d", cpu);
			break;

		case 'P':
			priority = atoi(optarg);
			break;

		case 'b':
			stop_upon_switch = 1;
			break;

		case 'm':
			max_latency_treshold = atoi(optarg);
			break;

		default:
			xenomai_usage();
			exit(2);
		}

	if (!test_duration && quiet) {
		warning("-q requires -T, ignoring -q");
		quiet = 0;
	}

	if (test_mode < USER_TASK || test_mode > TIMER_HANDLER)
		error(1, EINVAL, "invalid test mode");

#ifdef CONFIG_XENO_MERCURY
	if (test_mode != USER_TASK)
		error(1, EINVAL, "-t1, -t2 not allowed over Mercury");
#endif
	
	time(&test_start);

	histogram_avg = calloc(histogram_size, sizeof(int32_t));
	histogram_max = calloc(histogram_size, sizeof(int32_t));
	histogram_min = calloc(histogram_size, sizeof(int32_t));

	if (!(histogram_avg && histogram_max && histogram_min))
		cleanup();

	if (period_ns == 0)
		period_ns = CONFIG_XENO_DEFAULT_PERIOD;	/* ns */

	if (priority <= LOPRIO)
		priority = LOPRIO + 1;
	else if (priority > HIPRIO)
		priority = HIPRIO;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

#ifdef CONFIG_XENO_COBALT
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = sigdebug;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, NULL);
#endif

	if (freeze_max) {
		/* If something goes wrong, we want to freeze the current
		   trace path to help debugging. */
		signal(SIGSEGV, faulthand);
		signal(SIGBUS, faulthand);
	}

	setlinebuf(stdout);

	printf("== Sampling period: %Ld us\n"
	       "== Test mode: %s\n"
	       "== All results in microseconds\n",
	       period_ns / 1000, test_mode_names[test_mode]);

	if (test_mode != USER_TASK) {
		benchdev = open("/dev/rtdm/timerbench", O_RDWR);
		if (benchdev < 0)
			error(1, errno, "open sampler device (modprobe xeno_timerbench?)");
	}

	setup_sched_parameters(&tattr, 0);

	ret = pthread_create(&display_task, &tattr, display, NULL);
	if (ret)
		error(1, ret, "pthread_create(display)");

	pthread_attr_destroy(&tattr);

	if (test_mode == USER_TASK) {
		setup_sched_parameters(&tattr, priority);
		CPU_ZERO(&cpus);
		CPU_SET(cpu, &cpus);

		ret = pthread_attr_setaffinity_np(&tattr, sizeof(cpus), &cpus);
		if (ret)
			error(1, ret, "pthread_attr_setaffinity_np()");

		ret = pthread_create(&latency_task, &tattr, latency, NULL);
		if (ret)
			error(1, ret, "pthread_create(latency)");

		pthread_attr_destroy(&tattr);
	}

	__STD(sigwait(&mask, &sig));
	finished = 1;

	cleanup();

	return 0;
}
