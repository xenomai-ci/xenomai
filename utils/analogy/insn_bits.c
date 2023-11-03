/**
 * Analogy for Linux, instruction bits test program
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
#include <string.h>
#include <rtdm/analogy.h>

#define FILENAME "analogy0"

static char *filename = FILENAME;
static int verbose;
static int idx_subd = -1;

struct option insn_bits_opts[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void do_print_usage(void)
{
	fprintf(stdout, "usage:\tinsn_bits [OPTS] <bits_values> <mask>\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -d, --device: device filename (analogy0, analogy1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

int main(int argc, char *argv[])
{
	int i = 0, err = 0;
	a4l_desc_t dsc = { .sbdata = NULL };
	a4l_sbinfo_t *sbinfo;
	int scan_size, value, mask;

	/* Compute arguments */
	while ((err = getopt_long(argc,
				  argv,
				  "vd:s:h", insn_bits_opts,
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
		case 'h':
		default:
			do_print_usage();
			return 0;
		}
	}

	value = (argc - optind > 0) ? strtoul(argv[optind], NULL, 0) : 0;
	mask = (argc - optind > 1) ? strtoul(argv[optind + 1], NULL, 0) : 0;

	/* Open the device */
	err = a4l_open(&dsc, filename);
	if (err < 0) {
		fprintf(stderr,
			"insn_bits: a4l_open %s failed (err=%d)\n",
			filename, err);
		return err;
	}

	if (verbose != 0) {
		printf("insn_bits: device %s opened (fd=%d)\n", filename,
		       dsc.fd);
		printf("insn_bits: basic descriptor retrieved\n");
		printf("\t subdevices count = %d\n", dsc.nb_subd);
		printf("\t read subdevice index = %d\n", dsc.idx_read_subd);
		printf("\t write subdevice index = %d\n", dsc.idx_write_subd);
	}

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	dsc.sbdata = malloc(dsc.sbsize);
	if (dsc.sbdata == NULL) {
		err = -ENOMEM;
		fprintf(stderr, "insn_bits: info buffer allocation failed\n");
		goto out_insn_bits;
	}

	/* Get this data */
	err = a4l_fill_desc(&dsc);
	if (err < 0) {
		fprintf(stderr,
			"insn_bits: a4l_fill_desc failed (err=%d)\n", err);
		goto out_insn_bits;
	}

	if (verbose != 0)
		printf("insn_bits: complex descriptor retrieved\n");

	/* If no subdevice index was set, choose for the first digital
	   subdevice found */
	while (idx_subd == -1 && i < dsc.nb_subd) {

		err = a4l_get_subdinfo(&dsc, i, &sbinfo);
		if (err < 0) {
			fprintf(stderr,
				"insn_bits: get_sbinfo(%d) failed (err = %d)\n",
				i, err);
			goto out_insn_bits;
		}

		if ((sbinfo->flags & A4L_SUBD_TYPES) == A4L_SUBD_DIO ||
		    (sbinfo->flags & A4L_SUBD_TYPES) == A4L_SUBD_DI ||
		    (sbinfo->flags & A4L_SUBD_TYPES) == A4L_SUBD_DO) {
			idx_subd = i;
		}

		i++;
	}

	if (idx_subd == -1) {
		fprintf(stderr, "insn_bits: no digital subdevice available\n");
		err = -EINVAL;
		goto  out_insn_bits;
	}

	if (verbose != 0)
		printf("insn_bits: selected subdevice index = %d\n", idx_subd);

	/* We must check that the subdevice is really a digital one
	   (in case, the subdevice index was set with the option -s) */
	err = a4l_get_subdinfo(&dsc, idx_subd, &sbinfo);
	if (err < 0) {
		fprintf(stderr,
			"insn_bits: get_sbinfo(%d) failed (err = %d)\n",
			idx_subd, err);
		err = -EINVAL;
		goto out_insn_bits;
	}

	if ((sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_DIO &&
	    (sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_DI &&
	    (sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_DO) {
		fprintf(stderr,
			"insn_bits: selected subdevice is not digital\n");
		err = -EINVAL;
		goto out_insn_bits;
	}

	/* Set the data size to read / write */
	scan_size = a4l_sizeof_subd(sbinfo);

	if ((sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_DI) {
		printf("insn_bits: mask = 0x%x\n", mask);
		printf("insn_bits: value = 0x%x\n", value);
	}

	/* Handle little endian case with scan size < 32 */
	if (scan_size == sizeof(uint8_t)) {
		mask *= 0x01010101;
		value *= 0x01010101;
	}
	else if (scan_size == sizeof(uint16_t)) {
		mask *= 0x00010001;
		value *= 0x00010001;
	}

	/* Perform the synchronous operation */
	err = a4l_sync_dio(&dsc, idx_subd, &mask, &value);

	if (err < 0) {
		fprintf(stderr,
			"insn_bits: a4l_sync_dio() failed (err=%d)\n", err);
		goto out_insn_bits;
	}

	if (scan_size == sizeof(uint8_t)) {
		uint8_t tmp;
		memcpy(&tmp, &value, sizeof(uint8_t));
		value = (int)tmp;
	}
	else if (scan_size == sizeof(uint16_t)) {
		uint16_t tmp;
		memcpy(&tmp, &value, sizeof(uint16_t));
		value = (int)tmp;
	}

	if ((sbinfo->flags & A4L_SUBD_TYPES) != A4L_SUBD_DO)
		printf("insn_bits: result = 0x%x\n", value);
	else
		printf("insn_bits: operation succeeded\n");

out_insn_bits:

	/* Free the information buffer */
	if (dsc.sbdata != NULL)
		free(dsc.sbdata);

	/* Release the file descriptor */
	a4l_close(&dsc);

	return err;
}
