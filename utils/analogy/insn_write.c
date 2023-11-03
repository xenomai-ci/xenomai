/*
 * Analogy for Linux, instruction write test program
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
#include <errno.h>
#include <getopt.h>
#include <rtdm/analogy.h>

/* Ten triggered scans by default */
#define SCAN_CNT 10

#define FILENAME "analogy0"

#define BUF_SIZE 10000

static int value = 0;
static double dvalue = 0;
static char *filename = FILENAME;
static int verbose;
static int idx_subd = -1;
static int idx_chan;
static int idx_rng = -1;

struct option insn_write_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scan-count", required_argument, NULL, 'S'},
	{"channel", required_argument, NULL, 'c'},
	{"range", required_argument, NULL, 'R'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void do_print_usage(void)
{
	fprintf(stdout, "usage:\tinsn_write [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -d, --device: "
		"device filename (analogy0, analogy1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -c, --channel: channel to use\n");
	fprintf(stdout, "\t\t -R, --range: range to use\n");
	fprintf(stdout, "\t\t -V, --value: value to write\n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

int main(int argc, char *argv[])
{
	int err = 0;
	a4l_desc_t dsc = { .sbdata = NULL };
	a4l_sbinfo_t *sbinfo;
	a4l_chinfo_t *chinfo;
	a4l_rnginfo_t *rnginfo;
	unsigned int scan_size;

	/* Compute arguments */
	while ((err = getopt_long(argc,
				  argv,
				  "vd:s:c:R:V:h", insn_write_opts,
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
		case 'c':
			idx_chan = strtoul(optarg, NULL, 0);
			break;
		case 'R':
			idx_rng = strtoul(optarg, NULL, 0);
			break;
		case 'V':
			/* Do not perform the conversion until we know
			   which variable we need */
			break;
		case 'h':
		default:
			do_print_usage();
			return 0;
		}
	}

	/* Restart the argument scanning */
	optind = 1;

	while ((err = getopt_long(argc,
				  argv,
				  "vrd:s:c:R:V:h", insn_write_opts,
				  NULL)) >= 0) {
		switch (err) {
		case 'V':
			if (idx_rng < 0)
				value = (int)strtoul(optarg, NULL, 0);
			else
				dvalue = strtod(optarg, NULL);
		}
	}

	/* Open the device */
	err = a4l_open(&dsc, filename);
	if (err < 0) {
		fprintf(stderr,
			"insn_write: a4l_open %s failed (err=%d)\n",
			filename, err);
		return err;
	}

	if (verbose != 0) {
		printf("insn_write: device %s opened (fd=%d)\n", filename,
		       dsc.fd);
		printf("insn_write: basic descriptor retrieved\n");
		printf("\t subdevices count = %d\n", dsc.nb_subd);
		printf("\t read subdevice index = %d\n", dsc.idx_read_subd);
		printf("\t write subdevice index = %d\n", dsc.idx_write_subd);
	}

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		err = -ENOMEM;
		fprintf(stderr, "insn_write: info buffer allocation failed\n");
		goto out_insn_write;
	}

	/* Get this data */
	err = a4l_fill_desc(&dsc);
	if (err < 0) {
		fprintf(stderr, "insn_write: a4l_fill_desc failed (err=%d)\n",
			err);
		goto out_insn_write;
	}

	if (verbose != 0)
		printf("insn_write: complex descriptor retrieved\n");

	/* If no subdevice index was set, look for an analog output
	   subdevice */
	if (idx_subd == -1)
		idx_subd = dsc.idx_write_subd;

	if (idx_subd == -1) {
		fprintf(stderr,
			"insn_write: no analog output subdevice available\n");
		err = -EINVAL;
		goto  out_insn_write;
	}

	if (verbose != 0)
		printf("insn_write: selected subdevice index = %d\n", idx_subd);

	/* We must check that the subdevice is really an AO one
	   (in case, the subdevice index was set with the option -s) */
	err = a4l_get_subdinfo(&dsc, idx_subd, &sbinfo);
	if (err < 0) {
		fprintf(stderr,
			"insn_write: get_sbinfo(%d) failed (err = %d)\n",
			idx_subd, err);
		err = -EINVAL;
		goto out_insn_write;
	}

	if ((sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_AO) {
		fprintf(stderr,
			"insn_write: wrong subdevice selected "
			"(not an analog output)\n");
		err = -EINVAL;
		goto out_insn_write;
	}

	if (idx_rng >= 0) {

		err = a4l_get_rnginfo(&dsc,
				      idx_subd, idx_chan, idx_rng, &rnginfo);
		if (err < 0) {
			fprintf(stderr,
				"insn_write: failed to recover range descriptor\n");
			goto out_insn_write;
		}

		if (verbose != 0) {
			printf("insn_write: range descriptor retrieved\n");
			printf("\t min = %ld\n", rnginfo->min);
			printf("\t max = %ld\n", rnginfo->max);
		}
	}

	/* Retrieve the subdevice data size */
	err = a4l_get_chinfo(&dsc, idx_subd, idx_chan, &chinfo);
	if (err < 0) {
		fprintf(stderr,
			"insn_write: info for channel %d on subdevice %d "
			"not available (err=%d)\n",
			idx_chan, idx_subd, err);
		goto out_insn_write;
	}

	/* Set the data size to write */
	scan_size = (chinfo->nb_bits % 8 == 0) ?
		chinfo->nb_bits / 8 : (chinfo->nb_bits / 8) + 1;

	if (verbose != 0) {
		printf("insn_write: channel width is %u bits\n",
		       chinfo->nb_bits);
		printf("insn_write: global scan size is %u\n", scan_size);
	}

	/* If a range was selected, converts the samples */
	if (idx_rng >= 0) {
		if (a4l_dtoraw(chinfo, rnginfo, &value, &dvalue, 1) < 0) {
			fprintf(stderr,
				"insn_write: data conversion failed (err=%d)\n",
				err);
			goto out_insn_write;
		}

		if (verbose != 0)
			printf("insn_write: writing value %F (raw=0x%x)\n",
			       dvalue, value);

	} else if (verbose != 0)
		printf("insn_write: writing raw value 0x%x\n", value);

	/* Handle little endian case with bit range < 32 */
	if (scan_size == sizeof(char))
		value *= 0x01010101;
	else if (scan_size == sizeof(short))
		value *= 0x00010001;

	/* Perform the write operation */
	err = a4l_sync_write(&dsc,
			     idx_subd, CHAN(idx_chan), 0, &value, scan_size);

	if (err < 0) {
		fprintf(stderr,
			"insn_write: a4l_sync_write failed (err=%d)\n", err);
		goto out_insn_write;
	}

	if (verbose != 0)
		printf("insn_write: %u bytes successfully sent\n", scan_size);

out_insn_write:

	/* Free the information buffer */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	return err;
}
