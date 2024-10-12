/*
 * Copyright (C) 2016 Philippe Gerum <rpm@xenomai.org>
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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <smokey/smokey.h>
#include <linux/spi/spidev.h>
#include <rtdm/spi.h>

smokey_test_plugin(spi_transfer,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
			   SMOKEY_INT(speed),
			   SMOKEY_BOOL(latency),
			   SMOKEY_INT(ioctl_n)
		   ),
   "Run a SPI transfer.\n"
   "\tdevice=<device-path>\n"
   "\tspeed=<speed-hz>\n"
   "\tlatency\n"
   "\tioctl_n=<set to non-zero to use SPI_RTIOC_TRANSFER_N ioctl>"
);

#define ONE_BILLION	1000000000
#define TEN_MILLIONS	10000000

static int with_traffic = 1, with_latency, with_ioctl_n = 0;

#define SEQ_SHIFT 24
#define SEQ_MASK  ((1 << SEQ_SHIFT) - 1)

#define BAD_CRC  0x1
#define BAD_SEQ  0x2

struct frame_header {
	unsigned int seq: SEQ_SHIFT,
		crc : 8;
} __attribute__((packed));

/* We send a 32bit header followed by 32 bytes of payload. */
#define TRANSFER_SIZE (32 + sizeof(struct frame_header))

static unsigned char *i_area, *o_area;

static unsigned int seq_out;

static unsigned int seq_in = 1 << SEQ_SHIFT;

static int32_t minjitter, maxjitter, avgjitter;
static int32_t gminjitter = TEN_MILLIONS, gmaxjitter = -TEN_MILLIONS;
static uint32_t goverrun, gerrors;
static int64_t gavgjitter;

nanosecs_rel_t period_ns = ONE_BILLION / 2; /* 0.5s */

pthread_t display_tid, consumer_tid;

static sem_t display_sem;

static const int data_lines = 21;

static inline void *get_obuf(void)
{
	return o_area;
}

static inline void *get_odata(void)
{
	return o_area + sizeof(struct frame_header);
}

static inline size_t get_odlen(void)
{
	return TRANSFER_SIZE - sizeof(struct frame_header);
}

static void set_output_header(void)
{
	struct frame_header *fh = get_obuf();
	unsigned char *odata = get_odata();
	size_t odlen = get_odlen(), n;
	unsigned char csum;

	for (n = 0, csum = 0; n < odlen; n++)
		csum += *odata++;

	fh->crc = ~csum;
	fh->seq = seq_out;
	seq_out = (seq_out + 1) & SEQ_MASK;
}

static int check_input_header(void *ibuf, size_t ilen)
{
	struct frame_header *fh = ibuf;
	unsigned char *idata = ibuf + sizeof(*fh);
	size_t idlen = ilen - sizeof(*fh), n;
	unsigned int seq_next;
	unsigned char csum;
	int checkval = 0;

	for (n = 0, csum = 0; n < idlen; n++)
		csum += *idata++;

	if (fh->crc != (unsigned char)~csum)
		checkval |= BAD_CRC;

	if (seq_in > SEQ_MASK)
		seq_in = fh->seq;
	else {
		seq_next = (seq_in + 1) & SEQ_MASK;
		if (fh->seq != seq_next) {
			/* Try to resync. */
			seq_in = 1 << SEQ_SHIFT;
			checkval |= BAD_SEQ;
		} else
			seq_in = seq_next;
	}

	return checkval;
}

static void do_traffic(int round, const void *ibuf,
		       size_t ilen, int checkval)
{
	const struct frame_header *fh = ibuf;
	size_t idlen = ilen - sizeof(*fh), n;
	const unsigned char *idata = ibuf + sizeof(*fh);

	printf("%.4d> seq=%u%s, crc=%.2X%s",
	       round,
	       fh->seq, checkval & BAD_SEQ ? "?" : "",
	       fh->crc, checkval & BAD_CRC ? "?" : "");
	
	for (n = 0; n < idlen; n++) {
		if ((n % 16) == 0)
			printf("\n");
		printf("%.2X ", idata[n]);
	}
	printf("\n");
}

static int do_process(int round)
{
	size_t odlen, n, ilen = TRANSFER_SIZE;
	unsigned char *odata, *ibuf = i_area;
	int checkval;

	checkval = check_input_header(i_area, ilen);

	if (with_traffic)
		do_traffic(round, ibuf, ilen, checkval);

	odata = get_odata();
	odlen = get_odlen();
	for (n = 0; n < odlen; n++)
		odata[n] = odata[n] + 1 ?: 1;

	set_output_header();

	return checkval ? -EPROTO : 0;
}

static void timespec_add_ns(struct timespec *t, unsigned int ns)
{
	t->tv_nsec += ns;
	if (t->tv_nsec >= ONE_BILLION) {
		t->tv_nsec -= ONE_BILLION;
		t->tv_sec++;
	}
}

static inline long long diff_ts(struct timespec *left, struct timespec *right)
{
	return (long long)(left->tv_sec - right->tv_sec) * ONE_BILLION
		+ left->tv_nsec - right->tv_nsec;
}

static void *display_thread(void *arg)
{
	long minj, gminj, maxj, gmaxj, avgj;
	time_t start, now, dt;
	int ret, n = 0;

	sem_init(&display_sem, 0, 0);

	time(&start);

	for (;;) {
		ret = sem_wait(&display_sem);
		if (ret < 0) {
			if (errno != EIDRM)
				panic("sem_wait(), %s", symerror(errno));
			return NULL;
		}

		if (smokey_verbose_mode < 1)
			continue;
	
		minj = minjitter;
		gminj = gminjitter;
		avgj = avgjitter;
		maxj = maxjitter;
		gmaxj = gmaxjitter;

		if (data_lines && (n++ % data_lines) == 0) {
			time(&now);
			dt = now - start;
			printf("RTT|  %.2lld:%.2lld:%.2lld  (%lld us period)\n",
			       (long long)(dt / 3600),
			       (long long)((dt / 60) % 60),
			       (long long)(dt % 60),
			       (long long)period_ns / 1000);
			printf("RTH|%11s|%11s|%11s|%8s|%8s|%11s|%11s\n",
			       "----lat min", "----lat avg",
			       "----lat max", "-overrun", "-errors",
			       "---lat best", "--lat worst");
		}
		printf("RTD|%11.3f|%11.3f|%11.3f|%8d|%8d|%11.3f|%11.3f\n",
		       (double)minj / 1000,
		       (double)avgj / 1000,
		       (double)maxj / 1000,
		       goverrun,
		       gerrors,
		       (double)gminj / 1000, (double)gmaxj / 1000);
	}

	return NULL;
}

static void start_display_thread(void)
{
	struct sched_param param;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	param.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &param);
	pthread_create(&display_tid, &attr, display_thread, NULL);
}

static inline
nanosecs_rel_t get_start_delay(void)
{
	return ((1000000000ULL + period_ns - 1) / period_ns) * period_ns;
}

static int do_spi_loop(int fd)
{
	int ret, n, nsamples, loops = 0, tfd;
	struct timespec now, start;
	struct itimerspec its;

	memset(get_odata(), 0x1, get_odlen());
	set_output_header();

	if (with_latency) {
		nsamples = (long long)ONE_BILLION / period_ns;
		start_display_thread();
	} else
		nsamples = 1;

	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (tfd < 0)
		return -errno;

	clock_gettime(CLOCK_MONOTONIC, &start);
	timespec_add_ns(&start, get_start_delay());
	its.it_value = start;
	its.it_interval.tv_sec = period_ns / ONE_BILLION;
	its.it_interval.tv_nsec = period_ns % ONE_BILLION;
	ret = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &its, NULL);
	if (ret)
		return -errno;

	for (;;) {
		int32_t minj = TEN_MILLIONS, maxj = -TEN_MILLIONS, dt;
		uint32_t overrun = 0, errors = 0;
		uint64_t ticks;
		int64_t sumj;

		loops++;
	
		for (n = sumj = 0; n < nsamples; n++) {
			ret = read(tfd, &ticks, sizeof(ticks));
			if (ret < 0)
				break;
			clock_gettime(CLOCK_MONOTONIC, &start);
			if (with_ioctl_n == 0) {
				if (!__Terrno(ret,
					      ioctl(fd, SPI_RTIOC_TRANSFER)))
					return ret;
			} else {
                                if (!__Terrno(ret,
					ioctl(fd, SPI_RTIOC_TRANSFER_N,
					      TRANSFER_SIZE)))
                                        return ret;
			}
			if (with_latency) {
				clock_gettime(CLOCK_MONOTONIC, &now);
				dt = (int32_t)diff_ts(&now, &start);
				if (dt > maxj)
					maxj = dt;
				if (dt < minj)
					minj = dt;
				sumj += dt;
			}
		
			ret = do_process(loops);
			if (ret)
				errors++;
		}

		if (with_latency) {
			minjitter = minj;
			if (minj < gminjitter)
				gminjitter = minj;
			maxjitter = maxj;
			if (maxj > gmaxjitter)
				gmaxjitter = maxj;
			avgjitter = sumj / nsamples;
			gavgjitter += avgjitter;
			goverrun += overrun;
			gerrors += errors;
			sem_post(&display_sem);
		}		
	}

	return 0;
}

static int run_spi_transfer(struct smokey_test *t, int argc, char *const argv[])
{
	int fd, ret, speed_hz = 40000000;
	struct rtdm_spi_config config;
	struct rtdm_spi_iobufs iobufs;
	const char *device = NULL;
	struct sched_param param;
	void *p;
	
	smokey_parse_args(t, argc, argv);

	if (SMOKEY_ARG_ISSET(spi_transfer, latency) &&
	    SMOKEY_ARG_BOOL(spi_transfer, latency)) {
		with_latency = 1;
		/* Disable traffic tracing when monitoring latency. */
		with_traffic = 0;
	}

	if (SMOKEY_ARG_ISSET(spi_transfer, speed))
		speed_hz = SMOKEY_ARG_INT(spi_transfer, speed);

        if (SMOKEY_ARG_ISSET(spi_transfer, ioctl_n)) {
		with_ioctl_n = SMOKEY_ARG_INT(spi_transfer, ioctl_n);
		smokey_note("ioctl_n enabled; using SPI_RTIOC_TRANSFER_N");
	}

	if (!SMOKEY_ARG_ISSET(spi_transfer, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(spi_transfer, device);
	fd = open(device, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	iobufs.io_len = TRANSFER_SIZE;
	if (!__Terrno(ret, ioctl(fd, SPI_RTIOC_SET_IOBUFS, &iobufs)))
		return ret;

	p = mmap(NULL, iobufs.map_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (!__Fassert(p == MAP_FAILED))
		return -EINVAL;
	
	smokey_trace("input_area[%u..%u], output_area[%u..%u], mapping length=%u",
		     iobufs.i_offset, iobufs.i_offset + TRANSFER_SIZE - 1,
		     iobufs.o_offset, iobufs.o_offset + TRANSFER_SIZE - 1,
		     iobufs.map_len);
	
	i_area = p + iobufs.i_offset;
	o_area = p + iobufs.o_offset;

	config.mode = SPI_MODE_0;
	config.bits_per_word = 8;
	config.speed_hz = speed_hz;
	if (!__Terrno(ret, ioctl(fd, SPI_RTIOC_SET_CONFIG, &config)))
		return ret;

	if (!__Terrno(ret, ioctl(fd, SPI_RTIOC_GET_CONFIG, &config)))
		return ret;

	smokey_trace("speed=%u hz, mode=%#x, bits=%u",
		     config.speed_hz, config.mode, config.bits_per_word);
	
	/* Switch current thread to real-time. */
	param.sched_priority = 10;
	if (!__T(ret, pthread_setschedparam(pthread_self(),
				    SCHED_FIFO, &param)))
		return ret;

	if (!__T(ret, do_spi_loop(fd)))
		return ret;

	return 0;
}

int main(int argc, char *const argv[])
{
	struct smokey_test *t;
	int ret, fails = 0;

	if (pvlist_empty(&smokey_test_list))
		return 0;

	for_each_smokey_test(t) {
		ret = t->run(t, argc, argv);
		if (ret) {
			if (ret == -ENOSYS) {
				smokey_note("%s skipped (no kernel support)",
					    t->name);
				continue;
			}
			fails++;
			if (smokey_verbose_mode)
				smokey_note("test %s failed: %d", t->name,
					    -ret);
			if (smokey_keep_going)
				continue;
			return 1;
		}
		smokey_note("%s OK", t->name);
	}

	return fails != 0;
}
