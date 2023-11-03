/**
 * Analogy for Linux, instruction read test program
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
#include <errno.h>
#include <getopt.h>
#include <rtdm/analogy.h>

#define FILENAME "analogy0"
#define BUF_SIZE 10000
#define SCAN_CNT 10

static unsigned char buf[BUF_SIZE];
static char *filename = FILENAME;
static int verbose;
static int idx_subd = -1;
static int idx_chan;
static int idx_rng = -1;
static unsigned int scan_size = SCAN_CNT;
static char *calibration_file = NULL;

struct option insn_read_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scan-count", required_argument, NULL, 'S'},
	{"channel", required_argument, NULL, 'c'},
	{"range", required_argument, NULL, 'R'},
	{"cal", required_argument, NULL, 'y'},
	{"raw", no_argument, NULL, 'w'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void do_print_usage(void)
{
	fprintf(stdout, "usage:\tinsn_read [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -d, --device: device filename (analogy0, analogy1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -S, --scan-count: count of scan to perform\n");
	fprintf(stdout, "\t\t -c, --channel: channel to use\n");
	fprintf(stdout, "\t\t -R, --range: range to use\n");
	fprintf(stdout, "\t\t -w, --raw: dump data in raw format\n");
	fprintf(stdout, "\t\t -y, --cal: /path/to/calibration.bin \n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

static int dump_raw(a4l_desc_t *dsc, unsigned char *buf, int size)
{
	return fwrite(buf, size, 1, stdout);
}

static int dump_text(a4l_desc_t *dsc, unsigned char *buf, int size)
{
	int err = 0, width, tmp_size = 0;
	char *fmt;
	a4l_chinfo_t *chan;

	/* Retrieve the subdevice data size */
	err = a4l_get_chinfo(dsc, idx_subd, idx_chan, &chan);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: info for channel %d "
			"on subdevice %d not available (err=%d)\n",
			idx_chan, idx_subd, err);
		goto out;
	}

	width = a4l_sizeof_chan(chan);
	if (width < 0) {
		fprintf(stderr,
			"insn_read: incoherent info for channel %d\n",
			idx_chan);
		err = width;
		goto out;
	}

	switch(width) {
	case 1:
		fmt = "0x%02x\n";
		break;
	case 2:
		fmt = "0x%04x\n";
		break;
	case 4:
	default:
		fmt = "0x%08x\n";
		break;
	}

	while (size - tmp_size > 0) {
		unsigned long values[64];
		int i, tmp_cnt = ((size - tmp_size) / width > 64) ?
			64 : ((size - tmp_size) / width);

		err = a4l_rawtoul(chan, values, buf + tmp_size, tmp_cnt);
		if (err < 0)
			goto out;

		for (i = 0; i < tmp_cnt; i++)
			fprintf(stdout, fmt, values[i]);

		tmp_size += tmp_cnt * width;
	}

out:
	return err;
}

static int dump_converted(a4l_desc_t *dsc, unsigned char *buf, int size)
{
	int err = 0, width, tmp_size = 0;
	a4l_chinfo_t *chan;
	a4l_rnginfo_t *rng;

	/* Retrieve the channel info */
	err = a4l_get_chinfo(dsc, idx_subd, idx_chan, &chan);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: info for channel %d "
			"on subdevice %d not available (err=%d)\n",
			idx_chan, idx_subd, err);
		goto out;
	}

	/* Retrieve the range info */
	err = a4l_get_rnginfo(dsc, idx_subd, idx_chan, idx_rng, &rng);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: failed to recover range descriptor\n");
		goto out;
	}

	width = a4l_sizeof_chan(chan);
	if (width < 0) {
		fprintf(stderr,
			"insn_read: incoherent info for channel %d\n",
			idx_chan);
		err = width;
		goto out;
	}

	fprintf(stdout, "Non Calibrated values: \n");
	while (size - tmp_size > 0) {
		double values[64];
		int i, tmp_cnt = ((size - tmp_size) / width > 64) ?
			64 : ((size - tmp_size) / width);

		err = a4l_rawtod(chan, rng, values, buf + tmp_size, tmp_cnt);
		if (err < 0)
			goto out;

		for (i = 0; i < tmp_cnt; i++) {
			fprintf(stdout, "%F\n", values[i]);
		}

		tmp_size += tmp_cnt * width;
	}

out:
	return err;
}

static int dump_calibrated(a4l_desc_t *dsc, unsigned char *buf, int size)
{
	struct a4l_calibration_data cal_info;
	struct a4l_polynomial converter;
	int err = 0, width, tmp_size = 0;
	a4l_chinfo_t *chan;
	a4l_rnginfo_t *rng;


	/* Retrieve the channel info */
	err = a4l_get_chinfo(dsc, idx_subd, idx_chan, &chan);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: info for channel %d "
			"on subdevice %d not available (err=%d)\n",
			idx_chan, idx_subd, err);
		goto out;
	}

	/* Retrieve the range info */
	err = a4l_get_rnginfo(dsc, idx_subd, idx_chan, idx_rng, &rng);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: failed to recover range descriptor\n");
		goto out;
	}

	width = a4l_sizeof_chan(chan);
	if (width < 0) {
		fprintf(stderr,
			"insn_read: incoherent info for channel %d\n",
			idx_chan);
		err = width;
		goto out;
	}

	err = a4l_read_calibration_file(calibration_file, &cal_info);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: error reading the calibration file \n");
		goto out;
	}

	err = a4l_get_softcal_converter(&converter, idx_subd, idx_chan, idx_rng,
		                        &cal_info);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: failed to get the softcal converter  \n");
		goto out;
	}

	fprintf(stdout, "Calibrated values: \n");
	while (size - tmp_size > 0) {
		double values[64];
		int i, tmp_cnt = ((size - tmp_size) / width > 64) ?
			64 : ((size - tmp_size) / width);

		err = a4l_rawtodcal(chan, values, buf + tmp_size, tmp_cnt,
			            &converter);
		if (err < 0)
			goto out;

		for (i = 0; i < tmp_cnt; i++)
			fprintf(stdout, "%F\n", values[i]);

		tmp_size += tmp_cnt * width;
	}

out:
	return err;
}

int main(int argc, char *argv[])
{
	int err = 0;
	unsigned int cnt = 0;
	a4l_desc_t dsc = { .sbdata = NULL };
	a4l_sbinfo_t *sbinfo;
	a4l_chinfo_t *chinfo;
	a4l_rnginfo_t *rnginfo;

	int (*dump_function) (a4l_desc_t *, unsigned char *, int) = dump_text;

	/* Compute arguments */
	while ((err = getopt_long(argc,
				  argv,
				  "vrd:s:S:c:R:y:wh", insn_read_opts,
				  NULL)) >= 0) {
		switch (err) {
		case 'v':
			verbose = 1;
			break;
		case 'd':
			filename = optarg;
			break;
		case 's':
			idx_subd = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			scan_size = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			idx_chan = strtoul(optarg, NULL, 0);
			break;
		case 'R':
			idx_rng = strtoul(optarg, NULL, 0);
			dump_function = dump_converted;
			break;
		case 'w':
			dump_function = dump_raw;
			break;
		case 'y':
			dump_function = dump_calibrated;
			calibration_file = optarg;
			break;
		case 'h':
		default:
			do_print_usage();
			return 0;
		}
	}

	if (isatty(STDOUT_FILENO) && dump_function == dump_raw) {
		fprintf(stderr,
			"insn_read: cannot dump raw data on a terminal\n\n");
		return -EINVAL;
	}

	/* Open the device */
	err = a4l_open(&dsc, filename);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: a4l_open %s failed (err=%d)\n",
			filename, err);
		return err;
	}

	if (verbose != 0) {
		printf("insn_read: device %s opened (fd=%d)\n", filename,
		       dsc.fd);
		printf("insn_read: basic descriptor retrieved\n");
		printf("\t subdevices count = %d\n", dsc.nb_subd);
		printf("\t read subdevice index = %d\n", dsc.idx_read_subd);
		printf("\t write subdevice index = %d\n", dsc.idx_write_subd);
	}

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		err = -ENOMEM;
		fprintf(stderr, "insn_read: info buffer allocation failed\n");
		goto out_insn_read;
	}

	/* Get this data */
	err = a4l_fill_desc(&dsc);
	if (err < 0) {
		fprintf(stderr, "insn_read: a4l_fill_desc failed (err=%d)\n",
			err);
		goto out_insn_read;
	}

	if (verbose != 0)
		printf("insn_read: complex descriptor retrieved\n");

	/* If no subdevice index was set, look for an analog input
	   subdevice */
	if (idx_subd == -1)
		idx_subd = dsc.idx_read_subd;

	if (idx_subd == -1) {
		fprintf(stderr,
			"insn_read: no analog input subdevice available\n");
		err = -EINVAL;
		goto  out_insn_read;
	}

	if (verbose != 0)
		printf("insn_read: selected subdevice index = %d\n", idx_subd);

	/* We must check that the subdevice is really an AI one
	   (in case, the subdevice index was set with the option -s) */
	err = a4l_get_subdinfo(&dsc, idx_subd, &sbinfo);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: get_sbinfo(%d) failed (err = %d)\n",
			idx_subd, err);
		err = -EINVAL;
		goto out_insn_read;
	}

	if ((sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_AI) {
		fprintf(stderr,
			"insn_read: wrong subdevice selected "
			"(not an analog input)\n");
		err = -EINVAL;
		goto out_insn_read;
	}

	if (idx_rng >= 0) {

		err = a4l_get_rnginfo(&dsc,
				      idx_subd, idx_chan, idx_rng, &rnginfo);
		if (err < 0) {
			fprintf(stderr,
				"insn_read: failed to recover range descriptor\n");
			goto out_insn_read;
		}

		if (verbose != 0) {
			printf("insn_read: range descriptor retrieved\n");
			printf("\t min = %ld\n", rnginfo->min);
			printf("\t max = %ld\n", rnginfo->max);
		}
	}

	/* Retrieve the subdevice data size */
	err = a4l_get_chinfo(&dsc, idx_subd, idx_chan, &chinfo);
	if (err < 0) {
		fprintf(stderr,
			"insn_read: info for channel %d on subdevice %d not available (err=%d)\n",
			idx_chan, idx_subd, err);
		goto out_insn_read;
	}

	/* Set the data size to read */
	scan_size *= a4l_sizeof_chan(chinfo);

	if (verbose != 0) {
		printf("insn_read: channel width is %u bits\n",
		       chinfo->nb_bits);
		printf("insn_read: global scan size is %u\n", scan_size);
	}

	while (cnt < scan_size) {
		int tmp = (scan_size - cnt) < BUF_SIZE ?
			(scan_size - cnt) : BUF_SIZE;

		/* Perform the synchronous read */
		err = a4l_sync_read(&dsc,
				    idx_subd, CHAN(idx_chan), 0, buf, tmp);

		if (err < 0) {
			fprintf(stderr,
				"insn_read: a4l_sync_read failed (err=%d)\n",
				err);
			goto out_insn_read;
		}

		/* Dump the read data */
		tmp = dump_function(&dsc, buf, err);
		if (tmp < 0) {
			err = tmp;
			goto out_insn_read;
		}

		/* Update the count */
		cnt += err;
	}

	if (verbose != 0)
		printf("insn_read: %u bytes successfully received\n", cnt);

	err = 0;

out_insn_read:

	/* Free the information buffer */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	return err;
}
