/*
 * The alternate latency measurement program based on the Alchemy API.
 *
 * Licensed under the LGPL v2.1.
 */
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <alchemy/task.h>
#include <alchemy/timer.h>
#include <alchemy/sem.h>
#include <rtdm/testing.h>
#include <boilerplate/trace.h>
#include <xenomai/init.h>

RT_TASK latency_task, display_task;

RT_SEM display_sem;

#define TEN_MILLIONS    10000000

unsigned max_relaxed;
int32_t minjitter, maxjitter, avgjitter;
int32_t gminjitter = TEN_MILLIONS, gmaxjitter = -TEN_MILLIONS, goverrun = 0;
int64_t gavgjitter = 0;

long long period_ns = 0;
int test_duration = 0;		/* sec of testing, via -T <sec>, 0 is inf */
int data_lines = 21;		/* data lines per header line, -l <lines> to change */
int quiet = 0;			/* suppress printing of RTH, RTD lines when -T given */
int devfd = -1;
int freeze_max = 0;
int priority = T_HIPRIO;
int stop_upon_switch = 0;
sig_atomic_t sampling_relaxed = 0;

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

#define need_histo() (do_histogram || do_stats || do_gnuplot)

static inline void add_histogram(int32_t *histogram, int32_t addval)
{
	/* bucketsize steps */
	int inabs = (addval >= 0 ? addval : -addval) / bucketsize;
	histogram[inabs < histogram_size ? inabs : histogram_size - 1]++;
}

static void latency(void *cookie)
{
	RTIME expected_ns, start_ns, fault_threshold;
	unsigned int old_relaxed = 0, new_relaxed;
	int ret, count, nsamples, warmup = 1;
	int32_t minj, maxj, dt, overrun, sumj;
	unsigned long ov;

	fault_threshold = CONFIG_XENO_DEFAULT_PERIOD;
	nsamples = (long long)ONE_BILLION / period_ns;
	start_ns = rt_timer_read() + 1000000; /* 1ms from now */
	expected_ns = start_ns;

	ret = rt_task_set_periodic(NULL, start_ns, period_ns);
	if (ret) {
		fprintf(stderr, "altency: failed to set periodic, code %d\n",
			ret);
		return;
	}

	for (;;) {
		minj = TEN_MILLIONS;
		maxj = -TEN_MILLIONS;
		overrun = 0;
		test_loops++;

		for (count = sumj = 0; count < nsamples; count++) {
			ret = rt_task_wait_period(&ov);
			dt = (int32_t)(rt_timer_read() - expected_ns);
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

			if (ret) {
				if (ret != -ETIMEDOUT) {
					fprintf(stderr,
						"altency: wait period failed, code %d\n",
						ret);
					exit(EXIT_FAILURE); /* Timer stopped. */
				}
				overrun += ov;
				expected_ns += period_ns * ov;
			}
			expected_ns += period_ns;

			if (freeze_max && (dt > gmaxjitter)
			    && !(finished || warmup)) {
				xntrace_user_freeze(dt, 0);
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
			rt_sem_v(&display_sem);
		}

		if (warmup && test_loops == WARMUP_TIME) {
			test_loops = 0;
			warmup = 0;
		}
	}
}

static void display(void *cookie)
{
	char sem_name[16];
	int ret, n = 0;
	time_t start;

	if (test_mode == USER_TASK) {
		snprintf(sem_name, sizeof(sem_name), "dispsem-%d", getpid());
		ret = rt_sem_create(&display_sem, sem_name, 0, S_FIFO);
		if (ret) {
			fprintf(stderr,
				"altency: cannot create semaphore: %s\n",
				strerror(-ret));
			return;
		}

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

		ret = ioctl(devfd, RTTST_RTIOC_TMBENCH_START, &config);
		if (ret) {
			fprintf(stderr,
				"altency: failed to start in-kernel timer benchmark, code %d\n",
				ret);
			return;
		}
	}

	time(&start);

	if (WARMUP_TIME)
		printf("warming up...\n");

	if (quiet)
		fprintf(stderr, "running quietly for %d seconds\n",
			test_duration);

	for (;;) {
		int32_t minj, gminj, maxj, gmaxj, avgj;

		if (test_mode == USER_TASK) {
			ret = rt_sem_p(&display_sem, TM_INFINITE);
			if (ret) {
				if (ret != -EIDRM)
					fprintf(stderr,
						"altency: failed to pend on semaphore, code %d\n",
						ret);

				return;
			}

			minj = minjitter;
			gminj = gminjitter;
			avgj = avgjitter;
			maxj = maxjitter;
			gmaxj = gmaxjitter;

		} else {
			struct rttst_interm_bench_res result;

			ret = ioctl(devfd, RTTST_RTIOC_INTERM_BENCH_RES, &result);
			if (ret) {
				if (ret != -EIDRM)
					fprintf(stderr,
					"altency: failed to call RTTST_RTIOC_INTERM_BENCH_RES, %m\n");

				return;
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
				printf
				    ("RTT|  %.2ld:%.2ld:%.2ld  (%s, %Ld us period, "
				     "priority %d)\n", dt / 3600,
				     (dt / 60) % 60, dt % 60,
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

static void dump_histo_gnuplot(int32_t *histogram)
{
	unsigned start, stop;
	FILE *f;
	int n;

	f = fopen(do_gnuplot, "w");
	if (!f)
		return;

	for (n = 0; n < histogram_size && histogram[n] == 0L; n++)
		;
	start = n;

	for (n = histogram_size - 1; n >= 0 && histogram[n] == 0L; n--)
		;
	stop = n;

	fprintf(f, "%g 1\n", start * bucketsize / 1000.0);
	for (n = start; n <= stop; n++)
		fprintf(f, "%g %d\n",
			(n + 0.5) * bucketsize / 1000.0, histogram[n] + 1);
	fprintf(f, "%g 1\n", (stop + 1) * bucketsize / 1000.0);

	fclose(f);
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

static void dump_hist_stats(void)
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
		dump_histo_gnuplot(histogram_avg);
}

static void cleanup(void)
{
	time_t actual_duration;
	int32_t gmaxj, gminj, gavgj;

	if (test_mode == USER_TASK) {
		rt_sem_delete(&display_sem);

		gavgjitter /= (test_loops > 1 ? test_loops : 2) - 1;

		gminj = gminjitter;
		gmaxj = gmaxjitter;
		gavgj = gavgjitter;
	} else {
		struct rttst_overall_bench_res overall;

		overall.histogram_min = histogram_min;
		overall.histogram_max = histogram_max;
		overall.histogram_avg = histogram_avg;
		ioctl(devfd, RTTST_RTIOC_TMBENCH_STOP, &overall);
		gminj = overall.result.min;
		gmaxj = overall.result.max;
		gavgj = overall.result.avg;
		goverrun = overall.result.overruns;
	}

	if (devfd >= 0)
		close(devfd);

	if (need_histo())
		dump_hist_stats();

	time(&test_end);
	actual_duration = test_end - test_start - WARMUP_TIME;
	if (!test_duration)
		test_duration = actual_duration;

	printf
	    ("---|-----------|-----------|-----------|--------|------|-------------------------\n"
	     "RTS|%11.3f|%11.3f|%11.3f|%8d|%6u|    %.2ld:%.2ld:%.2ld/%.2d:%.2d:%.2d\n",
	     (double)gminj / 1000, (double)gavgj / 1000, (double)gmaxj / 1000,
	     goverrun, max_relaxed, actual_duration / 3600, (actual_duration / 60) % 60,
	     actual_duration % 60, test_duration / 3600,
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

	exit(0);
}

static void faulthand(int sig)
{
	xntrace_user_freeze(0, 1);
	signal(sig, SIG_DFL);
	__STD(kill(getpid(), sig));
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

	if (!stop_upon_switch) {
		++sampling_relaxed;
		return;
	}

	if (reason > SIGDEBUG_WATCHDOG)
		reason = SIGDEBUG_UNDEFINED;

	switch(reason) {
	case SIGDEBUG_UNDEFINED:
	case SIGDEBUG_NOMLOCK:
	case SIGDEBUG_WATCHDOG:
		n = snprintf(buffer, sizeof(buffer), "altency: %s\n",
			     reason_str[reason]);
		n = write(STDERR_FILENO, buffer, n);
		exit(EXIT_FAILURE);
	}

	n = snprintf(buffer, sizeof(buffer), fmt, reason_str[reason]);
	n = write(STDERR_FILENO, buffer, n);
	signal(sig, SIG_DFL);
	__STD(kill(getpid(), sig));
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
		);
}

int main(int argc, char *const *argv)
{
	struct sigaction sa __attribute__((unused));
	int c, ret, sig, cpu = 0;
	char task_name[32];
	cpu_set_t cpus;
	sigset_t mask;

	while ((c = getopt(argc, argv, "g:hp:l:T:qH:B:sD:t:fc:P:b")) != EOF)
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
			if (period_ns > ONE_BILLION) {
				fprintf(stderr, "altency: invalid period (> 1s).\n");
				exit(2);
			}
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
			if (cpu < 0 || cpu >= CPU_SETSIZE) {
				fprintf(stderr, "altency: invalid CPU #%d\n", cpu);
				return 1;
			}
			break;
		case 'P':
			priority = atoi(optarg);
			break;
		case 'b':
			stop_upon_switch = 1;
			break;
		default:
			xenomai_usage();
			exit(2);
		}

	if (!test_duration && quiet) {
		fprintf(stderr,
			"altency: -q only works if -T has been given.\n");
		quiet = 0;
	}

	if ((test_mode < USER_TASK) || (test_mode > TIMER_HANDLER)) {
		fprintf(stderr, "altency: invalid test mode.\n");
		exit(2);
	}

	time(&test_start);

	histogram_avg = calloc(histogram_size, sizeof(int32_t));
	histogram_max = calloc(histogram_size, sizeof(int32_t));
	histogram_min = calloc(histogram_size, sizeof(int32_t));

	if (!(histogram_avg && histogram_max && histogram_min))
		cleanup();

	if (period_ns == 0)
		period_ns = CONFIG_XENO_DEFAULT_PERIOD;	/* ns */

	if (priority <= T_LOPRIO)
		priority = T_LOPRIO + 1;
	else if (priority > T_HIPRIO)
		priority = T_HIPRIO;

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
		devfd = open("/dev/rtdm/timerbench", O_RDWR);
		if (devfd < 0) {
			fprintf(stderr,
				"altency: failed to open timerbench device, %m\n"
				"(modprobe xeno_timerbench?)\n");
			return 0;
		}
	}

	snprintf(task_name, sizeof(task_name), "alt-display-%d", getpid());
	ret = rt_task_create(&display_task, task_name, 0, 0, 0);
	if (ret) {
		fprintf(stderr,
			"altency: failed to create display task, code %d\n",
			ret);
		return 0;
	}

	ret = rt_task_start(&display_task, &display, NULL);
	if (ret) {
		fprintf(stderr,
			"altency: failed to start display task, code %d\n",
			ret);
		return 0;
	}

	if (test_mode == USER_TASK) {
		snprintf(task_name, sizeof(task_name), "alt-sampling-%d", getpid());
		ret = rt_task_create(&latency_task, task_name, 0, priority,
				     T_WARNSW);
		if (ret) {
			fprintf(stderr,
				"altency: failed to create sampling task, code %d\n",
				ret);
			return 0;
		}

		CPU_ZERO(&cpus);
		CPU_SET(cpu, &cpus);
		ret = rt_task_set_affinity(&latency_task, &cpus);
		if (ret) {
			fprintf(stderr,
				"altency: failed to set CPU affinity, code %d\n",
				ret);
			return 0;
		}

		ret = rt_task_start(&latency_task, latency, NULL);
		if (ret) {
			fprintf(stderr,
				"altency: failed to start sampling task, code %d\n",
				ret);
			return 0;
		}
	}

	__STD(sigwait(&mask, &sig));
	finished = 1;

	cleanup();

	return 0;
}
