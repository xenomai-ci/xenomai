/*
 * Copyright (C) 2011-2012 Gilles Chanteperdrix <gch@xenomai.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <mqueue.h>
#include <pthread.h>
#include <boilerplate/atomic.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <smokey/smokey.h>

smokey_test_plugin(posix_select,
		   SMOKEY_NOARGS,
		   "Check POSIX select service"
);

static const char *tunes[] = {
    "Surfing With The Alien",
    "Lords of Karma",
    "Banana Mango",
    "Psycho Monkey",
    "Luminous Flesh Giants",
    "Moroccan Sunset",
    "Satch Boogie",
    "Flying In A Blue Dream",
    "Ride",
    "Summer Song",
    "Speed Of Light",
    "Crystal Planet",
    "Raspberry Jam Delta-V",
    "Champagne?",
    "Clouds Race Across The Sky",
    "Engines Of Creation"
};

static int test_status;

static void *mq_thread(void *cookie)
{
	mqd_t mqd = (mqd_t)(long)cookie;
	unsigned int i = 0, prio;
	fd_set inset, tmp_inset;
	char buf[128];
	int ret;

	FD_ZERO(&inset);
	FD_SET(mqd, &inset);

	for (;;) {
		tmp_inset = inset;

		ret = smokey_check_errno(select(mqd + 1, &tmp_inset, NULL, NULL, NULL));
		if (ret < 0) {
			test_status = ret;
			break;
		}

		ret = smokey_check_errno(mq_receive(mqd, buf, sizeof(buf), &prio));
		if (ret < 0) {
			test_status = ret;
			break;
		}

		if (strcmp(buf, "/done") == 0)
			break;
	
		if (!smokey_assert(strcmp(buf, tunes[i]) == 0)) {
			test_status = -EINVAL;
			break;
		}

		smokey_trace("received %s", buf);
		i = (i + 1) % (sizeof(tunes) / sizeof(tunes[0]));
	}

	return NULL;
}

static int run_posix_select(struct smokey_test *t, int argc, char *const argv[])
{
	struct mq_attr qa;
	pthread_t tcb;
	int i, j, ret;
	mqd_t mq;

	mq_unlink("/select_test_mq");
	qa.mq_maxmsg = 128;
	qa.mq_msgsize = 128;
	mq = smokey_check_errno(mq_open("/select_test_mq", O_RDWR | O_CREAT | O_NONBLOCK, 0, &qa));
	if (mq < 0)
		return mq;

	ret = smokey_check_status(pthread_create(&tcb, NULL, mq_thread, (void *)(long)mq));
	if (ret)
		return ret;

	for (j = 0; j < 3; j++) {
		for (i = 0; i < sizeof(tunes) / sizeof(tunes[0]); i++) {
			ret = smokey_check_errno(mq_send(mq, tunes[i], strlen(tunes[i]) + 1, 0));
			if (ret < 0) {
				smokey_check_status(pthread_cancel(tcb));
				goto out;
			}
			usleep(100000);
		}
	}
	ret = smokey_check_errno(mq_send(mq, "/done", sizeof "/done", 0));
	if (ret < 0) {
		smokey_check_status(pthread_cancel(tcb));
		goto out;
	}
	usleep(300000);
	smp_rmb();
	ret = test_status;
out:
	pthread_join(tcb, NULL);

	mq_unlink("/select_test_mq");

	return ret;
}
