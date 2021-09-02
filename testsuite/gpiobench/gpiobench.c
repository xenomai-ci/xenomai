/*
 * Copyright (C) 2020 Song Chen <chensong@tj.kylinos.cn>
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
#include <rtdm/gpio.h>
#include <boilerplate/trace.h>
#include <xenomai/init.h>
#include <sys/mman.h>
#include <getopt.h>

#define NS_PER_MS (1000000)
#define NS_PER_S (1000000000)

#define DEFAULT_PRIO 99
#define VERSION_STRING "0.1"
#define GPIO_HIGH 1
#define GPIO_LOW  0
#define MAX_HIST		100
#define MAX_CYCLES 1000000
#define DEFAULT_LIMIT 1000
#define DEV_PATH    "/dev/rtdm/"
#define TRACING_ON  "/sys/kernel/debug/tracing/tracing_on"
#define TRACING_EVENTS  "/sys/kernel/debug/tracing/events/enable"
#define TRACE_MARKER  "/sys/kernel/debug/tracing/trace_marker"
#define ON  "1"
#define OFF "0"

enum {
	MODE_LOOPBACK,
	MODE_REACT,
	MODE_ALL
};

/* Struct for statistics */
struct test_stat {
	long inner_min;
	long inner_max;
	double inner_avg;
	long *inner_hist_array;
	long inner_hist_overflow;
	long outer_min;
	long outer_max;
	double outer_avg;
	long *outer_hist_array;
	long outer_hist_overflow;
};

/* Struct for information */
struct test_info {
	unsigned long max_cycles;
	unsigned long total_cycles;
	unsigned long max_histogram;
	int mode;
	int prio;
	int quiet;
	int tracelimit;
	int fd_dev_intr;
	int fd_dev_out;
	char pin_controller[32];
	pthread_t gpio_task;
	int gpio_intr;
	int gpio_out;
	struct test_stat ts;
};

struct test_info ti;
/* Print usage information */
static void display_help(void)
{
	printf("gpiobench V %s\n", VERSION_STRING);
	printf("Usage:\n"
	       "gpiobench <options>\n\n"

	       "-b       --breaktrace=USEC  send break trace command when latency > USEC\n"
	       "                            default=1000\n"
	       "-h       --histogram=US     dump a latency histogram to stdout after the run\n"
	       "                            US is the max time to be tracked in microseconds,\n"
	       "                            default=100\n"
	       "-l       --loops            number of loops, default=1000\n"
	       "-p       --prio             priority of highest prio thread, defaults=99\n"
	       "-q       --quiet            print only a summary on exit\n"
	       "-o       --output           gpio port number for output, no default value,\n"
	       "                            must be specified\n"
	       "-i       --intr             gpio port number as an interrupt, no default value,\n"
	       "                            must be specified\n"
	       "-c       --pinctrl          gpio pin controller's name, no default value,\n"
	       "                            must be specified\n"
	       "-m       --testmode         0 is loopback mode\n"
	       "                            1 is react mode which works with a latency box,\n"
	       "                            default=0\n\n"

	       "e.g.     gpiobench -o 20 -i 21 -c pinctrl-bcm2835\n"
		);
}

static void process_options(int argc, char *argv[])
{
	int c = 0;
	static const char optstring[] = "h:p:m:l:c:b:i:o:q";

	struct option long_options[] = {
		{ "bracetrace", required_argument, 0, 'b'},
		{ "histogram", required_argument, 0, 'h'},
		{ "loops", required_argument, 0, 'l'},
		{ "prio", required_argument, 0, 'p'},
		{ "quiet", no_argument, 0, 'q'},
		{ "output", required_argument, 0, 'o'},
		{ "intr", required_argument, 0, 'i'},
		{ "pinctrl", required_argument, 0, 'c'},
		{ "testmode", required_argument, 0, 'm'},
		{ 0, 0, 0, 0},
	};

	while ((c = getopt_long(argc, argv, optstring, long_options,
						NULL)) != -1) {
		switch (c) {
		case 'h':
			ti.max_histogram = atoi(optarg);
			break;

		case 'p':
			ti.prio = atoi(optarg);
			break;

		case 'l':
			ti.max_cycles = atoi(optarg);
			break;

		case 'q':
			ti.quiet = 1;
			break;

		case 'b':
			ti.tracelimit = atoi(optarg);
			break;

		case 'i':
			ti.gpio_intr = atoi(optarg);
			break;

		case 'o':
			ti.gpio_out = atoi(optarg);
			break;

		case 'c':
			strcpy(ti.pin_controller, optarg);
			break;

		case 'm':
			ti.mode = atoi(optarg) >=
				MODE_REACT ? MODE_REACT : MODE_LOOPBACK;
			break;

		default:
			display_help();
			exit(2);
		}
	}

	if ((ti.gpio_out == -1) || (ti.gpio_intr == -1)
				|| (strlen(ti.pin_controller) == 0)) {
		display_help();
		exit(2);
	}

	ti.prio = ti.prio > DEFAULT_PRIO ? DEFAULT_PRIO : ti.prio;
	ti.max_cycles = ti.max_cycles > MAX_CYCLES ? MAX_CYCLES : ti.max_cycles;

	ti.max_histogram = ti.max_histogram > MAX_HIST ?
		MAX_HIST : ti.max_histogram;
	ti.ts.inner_hist_array = calloc(ti.max_histogram, sizeof(long));
	ti.ts.outer_hist_array = calloc(ti.max_histogram, sizeof(long));
}

static int thread_msleep(unsigned int ms)
{
	struct timespec ts = {
		.tv_sec = (ms * NS_PER_MS) / NS_PER_S,
		.tv_nsec = (ms * NS_PER_MS) % NS_PER_S,
	};

	return -nanosleep(&ts, NULL);
}

static inline long long calc_us(struct timespec t)
{
	return ((long long)t.tv_sec * NS_PER_S + t.tv_nsec);
}

static int setevent(char *event, char *val)
{
	int fd;
	int ret;

	fd = open(event, O_WRONLY);
	if (fd < 0) {
		printf("unable to open %s\n", event);
		return -1;
	}

	ret = write(fd, val, strlen(val));
	if (ret < 0) {
		printf("unable to write %s to %s\n", val, event);
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

static void tracing(char *enable)
{
	setevent(TRACING_EVENTS, enable);
	setevent(TRACING_ON, enable);
}

#define write_check(__fd, __buf, __len)			\
	do {						\
		int __ret = write(__fd, __buf, __len);	\
		(void)__ret;				\
	} while (0)

#define TRACEBUFSIZ 1024
static __thread char tracebuf[TRACEBUFSIZ];

static void tracemark(char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tracemark(char *fmt, ...)
{
	va_list ap;
	int len;
	int tracemark_fd;

	tracemark_fd = open(TRACE_MARKER, O_WRONLY);
	if (tracemark_fd == -1) {
		printf("unable to open trace_marker file: %s\n", TRACE_MARKER);
		return;
	}

	/* bail out if we're not tracing */
	/* or if the kernel doesn't support trace_mark */
	if (tracemark_fd < 0)
		return;

	va_start(ap, fmt);
	len = vsnprintf(tracebuf, TRACEBUFSIZ, fmt, ap);
	va_end(ap);
	write_check(tracemark_fd, tracebuf, len);

	close(tracemark_fd);
}

static int rw_gpio(int value, int index)
{
	int ret;
	struct timespec timestamp;
	struct rtdm_gpio_readout rdo;
	long long gpio_write, gpio_read, inner_diff, outer_diff;

	clock_gettime(CLOCK_MONOTONIC, &timestamp);
	gpio_write = calc_us(timestamp);

	ret = write(ti.fd_dev_out, &value, sizeof(value));
	if (ret < 0) {
		printf("write GPIO, failed\n");
		return ret;
	}

	ret = read(ti.fd_dev_intr, &rdo, sizeof(struct rtdm_gpio_readout));
	if (ret < 0) {
		printf("read GPIO, failed\n");
		return ret;
	}

	clock_gettime(CLOCK_MONOTONIC, &timestamp);
	gpio_read = calc_us(timestamp);

	inner_diff = (rdo.timestamp - gpio_write) / 1000;
	outer_diff = (gpio_read - gpio_write) / 1000;

	if (inner_diff < ti.ts.inner_min)
		ti.ts.inner_min = inner_diff;
	if (inner_diff > ti.ts.inner_max)
		ti.ts.inner_max = inner_diff;
	ti.ts.inner_avg += (double) inner_diff;
	if (inner_diff >= ti.max_histogram)
		ti.ts.inner_hist_overflow++;
	else
		ti.ts.inner_hist_array[inner_diff]++;

	if (outer_diff < ti.ts.outer_min)
		ti.ts.outer_min = outer_diff;
	if (inner_diff > ti.ts.outer_max)
		ti.ts.outer_max = outer_diff;
	ti.ts.outer_avg += (double) outer_diff;
	if (outer_diff >= ti.max_histogram)
		ti.ts.outer_hist_overflow++;
	else
		ti.ts.outer_hist_array[outer_diff]++;

	if (ti.quiet == 0)
		printf("index: %d, inner_diff: %8lld, outer_diff: %8lld\n",
		       index, inner_diff, outer_diff);

	return outer_diff;
}

static void *run_gpiobench_loop(void *cookie)
{
	int i, ret;

	printf("----rt task, gpio loop, test run----\n");

	for (i = 0; i < ti.max_cycles; i++) {
		ti.total_cycles = i;
		/* send a high level pulse from gpio output pin and
		 * receive an interrupt from the other gpio pin,
		 * measuring the time elapsed between the two events
		 */
		ret = rw_gpio(GPIO_HIGH, i);
		if (ret < 0) {
			printf("RW GPIO, failed\n");
			break;
		} else if (ret > ti.tracelimit) {
			tracemark("hit latency threshold (%d > %d), index: %d",
						ret, ti.tracelimit, i);
			break;
		}

		/*take a break, nanosleep here will not jeopardize the latency*/
		thread_msleep(10);

		ret = rw_gpio(GPIO_LOW, i);
		/* send a low level pulse from gpio output pin and
		 * receive an interrupt from the other gpio pin,
		 * measuring the time elapsed between the two events
		 */
		if (ret < 0) {
			printf("RW GPIO, failed\n");
			break;
		} else if (ti.tracelimit && ret > ti.tracelimit) {
			tracemark("hit latency threshold (%d > %d), index: %d",
						ret, ti.tracelimit, i);
			break;
		}

		/*take a break, nanosleep here will not jeopardize the latency*/
		thread_msleep(10);
	}

	ti.ts.inner_avg /= (ti.total_cycles * 2);
	ti.ts.outer_avg /= (ti.total_cycles * 2);

	return NULL;
}

static void *run_gpiobench_react(void *cookie)
{
	int value, ret, i;
	struct rtdm_gpio_readout rdo;

	printf("----rt task, gpio react, test run----\n");

	for (i = 0; i < ti.max_cycles; i++) {
		/* received a pulse from latency box from one of
		 * the gpio pin pair
		 */
		ret = read(ti.fd_dev_intr, &rdo, sizeof(rdo));
		if (ret < 0) {
			printf("RW GPIO read, failed\n");
			break;
		}

		if (ti.quiet == 0)
			printf("idx: %d, received signal from latency box\n",
				    i);

		/* send a signal back from the other gpio pin
		 * to latency box as the acknowledge,
		 * latency box will measure the time elapsed
		 * between the two events
		 */
		value = GPIO_HIGH;
		ret = write(ti.fd_dev_out, &value, sizeof(value));
		if (ret < 0) {
			printf("RW GPIO write, failed\n");
			break;
		}

		if (ti.quiet == 0)
			printf("idx: %d, sent reaction to latency box\n", i);
	}

	return NULL;
}

static void setup_sched_parameters(pthread_attr_t *attr, int prio)
{
	struct sched_param p;
	int ret;

	ret = pthread_attr_init(attr);
	if (ret) {
		printf("pthread_attr_init(), failed\n");
		return;
	}

	ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
	if (ret) {
		printf("pthread_attr_setinheritsched(), failed\n");
		return;
	}

	ret = pthread_attr_setschedpolicy(attr,
				prio ? SCHED_FIFO : SCHED_OTHER);
	if (ret) {
		printf("pthread_attr_setschedpolicy(), failed\n");
		return;
	}

	p.sched_priority = prio;
	ret = pthread_attr_setschedparam(attr, &p);
	if (ret) {
		printf("pthread_attr_setschedparam(), failed\n");
		return;
	}
}

static void init_ti(void)
{
	memset(&ti, 0, sizeof(struct test_info));
	ti.prio = DEFAULT_PRIO;
	ti.max_cycles = MAX_CYCLES;
	ti.total_cycles = MAX_CYCLES;
	ti.max_histogram = MAX_HIST;
	ti.tracelimit = DEFAULT_LIMIT;
	ti.quiet = 0;
	ti.gpio_out = -1;
	ti.gpio_intr = -1;
	ti.mode = MODE_LOOPBACK;

	ti.ts.inner_min = ti.ts.outer_min = DEFAULT_LIMIT;
	ti.ts.inner_max = ti.ts.outer_max = 0;
	ti.ts.inner_avg = ti.ts.outer_avg = 0.0;
}

static void print_hist(void)
{
	int i;

	if (ti.total_cycles < (ti.max_cycles - 1)) {
		/*
		 * Test is interrupted. Force to calculate
		 * even though it is not accurate but to avoid
		 * large latency surprising us.
		 */
		printf("\nTest is interrupted and exit exceptionally\n");
		printf("Please run again till it exit normally\n");

		ti.ts.inner_avg /= (ti.total_cycles * 2);
		ti.ts.outer_avg /= (ti.total_cycles * 2);

	}
	printf("\n");
	printf("# Inner Loop Histogram\n");
	printf("# Inner Loop latency is the latency in kernel space\n"
		   "# between gpio_set_value and irq handler\n");

	for (i = 0; i < ti.max_histogram; i++) {
		unsigned long curr_latency = ti.ts.inner_hist_array[i];

		printf("%06d ", i);
		printf("%06lu\n", curr_latency);
	}

	printf("# Total:");
	printf(" %09lu", ti.total_cycles);
	printf("\n");

	printf("# Min Latencies:");
	printf(" %05lu", ti.ts.inner_min);
	printf("\n");
	printf("# Avg Latencies:");
	printf(" %05lf", ti.ts.inner_avg);
	printf("\n");
	printf("# Max Latencies:");
	printf(" %05lu", ti.ts.inner_max);
	printf("\n");

	printf("\n");
	printf("\n");

	printf("# Outer Loop Histogram\n");
	printf("# Outer Loop latency is the latency in user space\n"
		   "# between write and read\n"
		   "# Technically, outer loop latency is inner loop latercy\n"
		   "# plus overhead of event wakeup\n");

	for (i = 0; i < ti.max_histogram; i++) {
		unsigned long curr_latency = ti.ts.outer_hist_array[i];

		printf("%06d ", i);
		printf("%06lu\n", curr_latency);
	}

	printf("# Total:");
	printf(" %09lu", ti.total_cycles);
	printf("\n");

	printf("# Min Latencies:");
	printf(" %05lu", ti.ts.outer_min);
	printf("\n");
	printf("# Avg Latencies:");
	printf(" %05lf", ti.ts.outer_avg);
	printf("\n");
	printf("# Max Latencies:");
	printf(" %05lu", ti.ts.outer_max);
	printf("\n");
}

static void cleanup(void)
{
	int ret;

	if (ti.tracelimit < DEFAULT_LIMIT)
		tracing(OFF);

	ret = close(ti.fd_dev_out);
	if (ret < 0)
		printf("can't close gpio_out device\n");

	ret = close(ti.fd_dev_intr);
	if (ret < 0)
		printf("can't close gpio_intr device\n");

	if (ti.mode == MODE_LOOPBACK)
		print_hist();

}

static void cleanup_and_exit(int sig)
{
	printf("Signal %d received\n", sig);
	cleanup();
	exit(0);
}

int main(int argc, char **argv)
{
	struct sigaction sa __attribute__((unused));
	int ret = 0;
	pthread_attr_t tattr;
	int trigger, value;
	char dev_name[64];

	init_ti();

	process_options(argc, argv);

	ret = mlockall(MCL_CURRENT|MCL_FUTURE);
	if (ret) {
		printf("mlockall failed\n");
		goto out;
	}

	sprintf(dev_name, "%s%s/gpio%d",
		    DEV_PATH, ti.pin_controller, ti.gpio_out);
	ti.fd_dev_out = open(dev_name, O_RDWR);
	if (ti.fd_dev_out < 0) {
		printf("can't open %s\n", dev_name);
		goto out;
	}

	if (ti.gpio_out) {
		value = 0;
		ret = ioctl(ti.fd_dev_out, GPIO_RTIOC_DIR_OUT, &value);
		if (ret) {
			printf("ioctl gpio port output, failed\n");
			goto out;
		}
	}

	sprintf(dev_name, "%s%s/gpio%d",
		    DEV_PATH, ti.pin_controller, ti.gpio_intr);
	ti.fd_dev_intr = open(dev_name, O_RDWR);
	if (ti.fd_dev_intr < 0) {
		printf("can't open %s\n", dev_name);
		goto out;
	}

	if (ti.gpio_intr) {
		trigger = GPIO_TRIGGER_EDGE_FALLING|GPIO_TRIGGER_EDGE_RISING;
		value = 1;

		ret = ioctl(ti.fd_dev_intr, GPIO_RTIOC_IRQEN, &trigger);
		if (ret) {
			printf("ioctl gpio port interrupt, failed\n");
			goto out;
		}

		ret = ioctl(ti.fd_dev_intr, GPIO_RTIOC_TS_MONO, &value);
		if (ret) {
			printf("ioctl gpio port ts, failed\n");
			goto out;
		}
	}

	if (ti.tracelimit < DEFAULT_LIMIT)
		tracing(ON);

	signal(SIGTERM, cleanup_and_exit);
	signal(SIGINT, cleanup_and_exit);
	setup_sched_parameters(&tattr, ti.prio);

	if (ti.mode == MODE_LOOPBACK)
		ret = pthread_create(&ti.gpio_task, &tattr,
					run_gpiobench_loop, NULL);
	else
		ret = pthread_create(&ti.gpio_task, &tattr,
					run_gpiobench_react, NULL);

	if (ret) {
		printf("pthread_create(gpiotask), failed\n");
		goto out;
	}

	pthread_join(ti.gpio_task, NULL);
	pthread_attr_destroy(&tattr);

out:
	cleanup();
	return 0;
}
