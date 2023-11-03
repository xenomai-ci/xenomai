/*
 * Analogy for Linux, input command test program
 *
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <string.h>
#include <pthread.h>
#include <rtdm/analogy.h>

typedef int (*dump_function_t) (a4l_desc_t *, a4l_cmd_t*, unsigned char *, int);

struct arguments {
	int argc;
	char **argv;
};

#define MAX_NB_CHAN 32
#define NB_SCAN 100
#define ID_SUBD 0

#define FILENAME "analogy0"
#define BUF_SIZE 10000

static unsigned int chans[MAX_NB_CHAN];
static unsigned char buf[BUF_SIZE];
static char *str_chans = "0,1,2,3";
static char *filename = FILENAME;

static unsigned long wake_count = 0;
static int real_time = 0;
static int use_mmap = 0;
static int verbose = 0;

#define exit_err(fmt, args ...) error(1,0, fmt "\n", ##args)
#define output(fmt, args ...) fprintf(stdout, fmt "\n", ##args)
#define debug(fmt, args...)  if (verbose &&  printf(fmt "\n", ##args))

/* The command to send by default */
a4l_cmd_t cmd = {
	.idx_subd = ID_SUBD,
	.flags = 0,
	.start_src = TRIG_NOW,
	.start_arg = 0,
	.scan_begin_src = TRIG_TIMER,
	.scan_begin_arg = 8000000,	/* in ns */
	.convert_src = TRIG_TIMER,
	.convert_arg = 500000,	/* in ns */
	.scan_end_src = TRIG_COUNT,
	.scan_end_arg = 0,
	.stop_src = TRIG_COUNT,
	.stop_arg = NB_SCAN,
	.nb_chan = 0,
	.chan_descs = chans,
};

struct option cmd_read_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"real-time", no_argument, NULL, 'r'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scan-count", required_argument, NULL, 'S'},
	{"channels", required_argument, NULL, 'c'},
	{"mmap", no_argument, NULL, 'm'},
	{"raw", no_argument, NULL, 'w'},
	{"wake-count", required_argument, NULL, 'k'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void do_print_usage(void)
{
	output("usage:\tcmd_read [OPTS]");
	output("\tOPTS:\t -v, --verbose: verbose output");
	output("\t\t -r, --real-time: enable real-time acquisition mode");
	output("\t\t -d, --device: device filename (analogy0, analogy1, ...)");
	output("\t\t -s, --subdevice: subdevice index");
	output("\t\t -S, --scan-count: count of scan to perform");
	output("\t\t -c, --channels: channels to use (ex.: -c 0,1)");
	output("\t\t -m, --mmap: mmap the buffer");
	output("\t\t -w, --raw: dump data in raw format");
	output("\t\t -k, --wake-count: space available before waking up the process");
	output("\t\t -h, --help: output this help");
}

static inline int dump_raw(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	return fwrite(buf, size, 1, stdout);
}

static int dump_text(a4l_desc_t *dsc, a4l_cmd_t *cmd, unsigned char *buf, int size)
{
	a4l_chinfo_t *chans[MAX_NB_CHAN];
	int i, err = 0, tmp_size = 0;
	char *fmts[MAX_NB_CHAN];
	static int cur_chan;

	for (i = 0; i < cmd->nb_chan; i++) {
		int width;

		err = a4l_get_chinfo(dsc, cmd->idx_subd, cmd->chan_descs[i], &chans[i]);
		if (err < 0)
			exit_err("a4l_get_chinfo failed (ret=%d)", err);

		width = a4l_sizeof_chan(chans[i]);
		if (width < 0)
			exit_err("incoherent info for channel %d", cmd->chan_descs[i]);

		switch (width) {
		case 1:
			fmts[i] = "0x%02x ";
			break;
		case 2:
			fmts[i] = "0x%04x ";
			break;
		case 4:
		default:
			fmts[i] = "0x%08x ";
			break;
		}
	}

	while (tmp_size < size) {
		unsigned long value;
		err = a4l_rawtoul(chans[cur_chan], &value, buf + tmp_size, 1);
		if (err < 0)
			goto out;

		fprintf(stdout, fmts[cur_chan], value);

		/* We assume a4l_sizeof_chan() cannot return because we already
		 * called it on the very same channel descriptor */
		tmp_size += a4l_sizeof_chan(chans[cur_chan]);

		if (++cur_chan == cmd->nb_chan) {
			fprintf(stdout, "\n");
			cur_chan = 0;
		}
	}

	fflush(stdout);
out:
	return err;
}

static int fetch_data(a4l_desc_t *dsc, void *buf, unsigned int *cnt, dump_function_t dump)
{
	int ret;

	for (;;) {
		ret = a4l_async_read(dsc, buf, BUF_SIZE, A4L_INFINITE);

		if (ret == 0) {
			debug("no more data in the buffer ");
			break;
		}

		if (ret < 0)
			exit_err("a4l_read failed (ret=%d)", ret);

		*cnt += ret;

		ret = dump(dsc, &cmd, buf, ret);
		if (ret < 0)
			return -EIO;
	}

	return ret;
}

static int fetch_data_mmap(a4l_desc_t *dsc, unsigned int *cnt, dump_function_t dump,
			   void *map, unsigned long buf_size)
{
	unsigned long cnt_current = 0, cnt_updated = 0;
	int ret;

	for (;;) {

		/* Retrieve and update the buffer's state
		 * In input case, recover how many bytes are available to read
		 */
		ret = a4l_mark_bufrw(dsc, cmd.idx_subd, cnt_current, &cnt_updated);

		if (ret == -ENOENT)
			break;

		if (ret < 0)
			exit_err("a4l_mark_bufrw() failed (ret=%d)", ret);

		/* If there is nothing to read, wait for an event
		   (Note that a4l_poll() also retrieves the data amount
		   to read; in our case it is useless as we have to update
		   the data read counter) */
		if (!cnt_updated) {
			ret = a4l_poll(dsc, cmd.idx_subd, A4L_INFINITE);
			if (ret < 0)
				exit_err("a4l_poll() failed (ret=%d)", ret);

			if (ret == 0)
				break;

			cnt_current = cnt_updated;
			continue;
		}

		ret = dump(dsc, &cmd, map + (*cnt % buf_size), cnt_updated);
		if (ret < 0)
			return -EIO;

		*cnt += cnt_updated;
		cnt_current = cnt_updated;
	}

	return 0;
}

static int map_subdevice_buffer(a4l_desc_t *dsc, unsigned long *buf_size, void **map)
{
	void *buf;
	int ret;

	/* Get the buffer size to map */
	ret = a4l_get_bufsize(dsc, cmd.idx_subd, buf_size);
	if (ret < 0)
		exit_err("a4l_get_bufsize() failed (ret=%d)", ret);
	debug("buffer size = %lu bytes", *buf_size);

	/* Map the analog input subdevice buffer */
	ret = a4l_mmap(dsc, cmd.idx_subd, *buf_size, &buf);
	if (ret < 0)
		exit_err("a4l_mmap() failed (ret=%d)", ret);
	debug("mmap done (map=0x%p)", buf);

	*map = buf;

	return 0;
}

static int cmd_read(struct arguments *arg)
{
	unsigned int i, scan_size = 0, cnt = 0, len, ofs;
	dump_function_t dump_function = dump_text;
	a4l_desc_t dsc = { .sbdata = NULL };
	unsigned long buf_size;
	char **argv = arg->argv;
	int ret = 0, argc = arg->argc;
	void *map = NULL;

	for (;;) {
		ret = getopt_long(argc, argv, "vrd:s:S:c:mwk:h",
				  cmd_read_opts, NULL);

		if (ret == -1)
			break;

		switch (ret) {
		case 'v':
			verbose = 1;
			break;
		case 'r':
			real_time = 1;
			break;
		case 'd':
			filename = optarg;
			break;
		case 's':
			cmd.idx_subd = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			cmd.stop_arg = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			str_chans = optarg;
			break;
		case 'm':
			use_mmap = 1;
			break;
		case 'w':
			dump_function = dump_raw;
			break;
		case 'k':
			wake_count = strtoul(optarg, NULL, 0);
			break;
		case 'h':
		default:
			do_print_usage();
			return -EINVAL;
		}
	}

	if (isatty(STDOUT_FILENO) && dump_function == dump_raw)
		exit_err("cannot dump raw data on a terminal\n");

	/* Recover the channels to compute */
	do {
		cmd.nb_chan++;
		len = strlen(str_chans);
		ofs = strcspn(str_chans, ",");

		if (sscanf(str_chans, "%u", &chans[cmd.nb_chan - 1]) == 0)
			exit_err("bad channel argument");

		str_chans += ofs + 1;
	} while (len != ofs);

	/* Update the command structure */
	cmd.scan_end_arg = cmd.nb_chan;
	cmd.stop_src = cmd.stop_arg != 0 ? TRIG_COUNT : TRIG_NONE;

	ret = a4l_open(&dsc, filename);
	if (ret < 0)
		exit_err("a4l_open %s failed (ret=%d)", filename, ret);

	debug("device %s opened (fd=%d)", filename, dsc.fd);
	debug("basic descriptor retrieved");
	debug("\t subdevices count = %d", dsc.nb_subd);
	debug("\t read subdevice index = %d", dsc.idx_read_subd);
	debug("\t write subdevice index = %d", dsc.idx_write_subd);

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL)
		exit_err("malloc failed ");

	/* Get this data */
	ret = a4l_fill_desc(&dsc);
	if (ret < 0)
		exit_err("a4l_fill_desc failed (ret=%d)", ret);
	debug("complex descriptor retrieved");

	/* Get the size of a single acquisition */
	for (i = 0; i < cmd.nb_chan; i++) {
		a4l_chinfo_t *info;

		ret = a4l_get_chinfo(&dsc,cmd.idx_subd, cmd.chan_descs[i], &info);
		if (ret < 0)
			exit_err("a4l_get_chinfo failed (ret=%d)", ret);

		debug("channel %x", cmd.chan_descs[i]);
		debug(" ranges count = %d", info->nb_rng);
		debug(" bit width = %d (bits)", info->nb_bits);

		scan_size += a4l_sizeof_chan(info);
	}

	debug("size to read = %u", scan_size * cmd.stop_arg);
	debug("scan size = %u", scan_size);

	/* Cancel any former command which might be in progress */
	a4l_snd_cancel(&dsc, cmd.idx_subd);

	if (use_mmap) {
		ret = map_subdevice_buffer(&dsc, &buf_size, &map);
		if (ret)
			goto out;
	}

	ret = a4l_set_wakesize(&dsc, wake_count);
	if (ret < 0)
		exit_err("a4l_set_wakesize failed (ret=%d)", ret);
	debug("wake size successfully set (%lu)", wake_count);

	/* Send the command to the input device */
	ret = a4l_snd_command(&dsc, &cmd);
	if (ret < 0)
		exit_err("a4l_snd_command failed (ret=%d)", ret);
	debug("command sent");

	if (use_mmap) {
		ret = fetch_data_mmap(&dsc, &cnt, dump_function, map, buf_size);
		if (ret)
			exit_err("failed to fetch_data_mmap (ret=%d)", ret);
	}
	else {
		ret = fetch_data(&dsc, buf, &cnt, dump_function);
		if (ret)
			exit_err("failed to fetch_data (ret=%d)", ret);
	}
	debug("%d bytes successfully received (ret=%d)", cnt, ret);

	return 0;

out:
	if (use_mmap)
		munmap(map, buf_size);

	/* Free the buffer used as device descriptor */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	return ret;
}

int main(int argc, char *argv[])
{
	struct sched_param param = {.sched_priority = 99};
	struct arguments args = {.argc = argc, .argv = argv};
	int ret;

	ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	if (ret)
		exit_err("pthread_setschedparam failed (ret=0x%x) ", ret);

	ret = cmd_read(&args);
	if (ret)
		exit_err("cmd_read error (ret=0x%x) ", ret);

	return ret;
}
