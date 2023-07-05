/*
 * Functional testing of RTDM services.
 *
 * Copyright (C) 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <rtdm/testing.h>
#include <smokey/smokey.h>

smokey_test_plugin(rtdm,
		   SMOKEY_NOARGS,
		   "Check core interface to RTDM services."
);

#define NS_PER_MS (1000000)

static inline unsigned long long timer_get_tsc(void)
{
	return clockobj_get_tsc();
}

static inline unsigned long long timer_tsc2ns(unsigned long long tsc)
{
	return clockobj_tsc_to_ns(tsc);
}

static void check_inner(const char *fn, int line, const char *msg,
			int status, int expected)
{
	if (status == expected)
		return;

	fprintf(stderr, "FAILED %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(-status));
	exit(EXIT_FAILURE);
}

#define check(msg, status, expected) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,			\
		    __status < 0 ? -errno : __status, expected);	\
	__status;							\
})

#define check_no_error(msg, status) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,			\
		    __status < 0 ? -errno : 0, 0);			\
	__status;							\
})

static const char *devname = "/dev/rtdm/rtdm0";
static const char *devname2 = "/dev/rtdm/rtdm1";

static int do_handover(int fd)
{
	struct sched_param param;
	int ret, magic = 0;

	if (!__F(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_PRIMARY, &magic)) ||
	    errno != ENOTTY)
		return ret ? -ENOTTY : -EINVAL;

	if (!__Tassert(magic == 0))
		return -EINVAL;

	if (!__Terrno(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_SECONDARY, &magic)))
		return ret;

	if (!__Tassert(magic == RTTST_RTDM_MAGIC_SECONDARY))
		return -EINVAL;

	/* Switch to Cobalt's SCHED_FIFO[1] */
	
	param.sched_priority = 1;
	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    SCHED_FIFO, &param)))
		return ret;
	
	if (!__Terrno(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_PRIMARY, &magic)))
		return ret;

	if (!__Tassert(magic == RTTST_RTDM_MAGIC_PRIMARY))
		return -EINVAL;

	if (!__Terrno(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_SECONDARY, &magic)))
		return ret;

	if (!__Tassert(magic == RTTST_RTDM_MAGIC_SECONDARY))
		return -EINVAL;

	/* Switch to Cobalt's SCHED_WEAK[0] */
	
	param.sched_priority = 0;
	if (!__T(ret, pthread_setschedparam(pthread_self(),
					    SCHED_WEAK, &param)))
		return ret;
	
	if (!__Terrno(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_PRIMARY, &magic)))
		return ret;

	if (!__Tassert(magic == RTTST_RTDM_MAGIC_PRIMARY))
		return -EINVAL;

	if (!__Terrno(ret, ioctl(fd, RTTST_RTIOC_RTDM_PING_SECONDARY, &magic)))
		return ret;

	if (!__Tassert(magic == RTTST_RTDM_MAGIC_SECONDARY))
		return -EINVAL;

	return 0;
}

static void *__test_handover(void *arg)
{
	int fd = *(int *)arg;

	return (void *)(long)do_handover(fd);
}

static int test_handover(int fd)
{
	struct sched_param param;
	pthread_attr_t attr;
	pthread_t tid;
	void *p;
	int ret;

	pthread_attr_init(&attr);
	param.sched_priority = 0;
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
	pthread_attr_setschedparam(&attr, &param);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

	if (!__T(ret, __STD(pthread_create(&tid, &attr,
			   __test_handover, &fd))))
		return ret;

	if (!__T(ret, pthread_join(tid, &p)))
		return ret;

	return (int)(long)p;
}

static int run_rtdm(struct smokey_test *t, int argc, char *const argv[])
{
	int dev, dev2, status;

	status = smokey_modprobe("xeno_rtdmtest", true);
	if (status)
		return -ENOSYS;

	if (access(devname, 0) < 0 && errno == ENOENT)
		return -ENOSYS;

	smokey_trace("Setup");
	dev = check_no_error("open", open(devname, O_RDWR));

	smokey_trace("Exclusive open");
	check("open", open(devname, O_RDWR), -EBUSY);

	smokey_trace("Successive open");
	dev2 = check("open", open(devname2, O_RDWR), dev + 1);
	check("close", close(dev2), 0);

	smokey_trace("Handover mode");
	status = test_handover(dev);
	if (status)
		return status;

	smokey_trace("Defer close by pending reference");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_DEFER_CLOSE_CONTEXT), 0);
	check("close", close(dev), 0);
	check("open", open(devname, O_RDWR), -EBUSY);
	dev2 = check("open", open(devname2, O_RDWR), dev);
	check("close", close(dev2), 0);
	usleep(smokey_on_vm ? 400000 : 301000);
	dev = check("open", open(devname, O_RDWR), dev);

	smokey_trace("Normal close");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_NORMAL_CLOSE), 0);
	check("close", close(dev), 0);
	dev = check("open", open(devname, O_RDWR), dev);

	smokey_trace("Disconnect on module unload");
	check("ioctl", ioctl(dev, RTTST_RTIOC_RTDM_DEFER_CLOSE,
			     RTTST_RTDM_DEFER_CLOSE_CONTEXT), 0);
	check("close", close(dev), 0);

	smokey_rmmod("xeno_rtdmtest");

	return 0;
}
