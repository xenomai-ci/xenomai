/*
 * Analogy for Linux, output command test program
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
#include <string.h>
#include <rtdm/analogy.h>

#define BUFFER_DEPTH 1024

struct config {

	/* Configuration parameters
	   TODO: add real_time and use_mmap*/

	int verbose;

	int subd;
	char *str_chans;
	unsigned int *chans;
	int chans_count;
	char *str_ranges;
	unsigned long scans_count;
	unsigned long wake_count;

	char *filename;
	FILE *input;

	/* Analogy stuff */

	a4l_desc_t dsc;
	a4l_chinfo_t *cinfo;
	a4l_rnginfo_t *rinfo;

	/* Buffer stuff
	   TODO: add buffer depth / size (useful for mmap) */
	void *buffer;

};

/* --- Options / arguments part --- */

struct option options[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"device", required_argument, NULL, 'd'},
	{"subdevice", required_argument, NULL, 's'},
	{"scans-count", required_argument, NULL, 'S'},
	{"channels", required_argument, NULL, 'c'},
	{"range", required_argument, NULL, 'c'},
	{"wake-count", required_argument, NULL, 'k'},
	{"input", required_argument, NULL, 'i'},
	{"help", no_argument, NULL, 'h'},
	{0},
};

static void print_usage(void)
{
	fprintf(stdout, "usage:\tcmd_write [OPTS]\n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout,
		"\t\t -d, --device: "
		"device filename (analogy0, analogy1, ...)\n");
	fprintf(stdout, "\t\t -s, --subdevice: subdevice index\n");
	fprintf(stdout, "\t\t -S, --scans-count: count of scan to perform\n");
	fprintf(stdout,
		"\t\t -c, --channels: "
		"channels to use <i,j,...> (ex.: -c 0,1)\n");
	fprintf(stdout,
		"\t\t -R, --range: "
		"range to use <min,max,unit> (ex.: -R 0,1,V)\n");
	fprintf(stdout,
		"\t\t -k, --wake-count: "
		"space available before waking up the process\n");
	fprintf(stdout,
		"\t\t -i, --input: file to use for input  (default stdin) \n"
		"\t\t\t      use wf_generate to create the file\n");

	fprintf(stdout, "\t\t -h, --help: print this help\n");
}

/* --- Configuration related stuff --- */

static int init_dsc_config(struct config *cfg)
{
	int err = 0;

	/* Here we have to open the Analogy device file */
	err = a4l_open(&cfg->dsc, cfg->filename);
	if (err < 0) {
		fprintf(stderr, "cmd_write: a4l_open %s failed (ret=%d)\n",
			cfg->filename, err);
		goto error;
	}

	/* Allocate a buffer so as to get more info (subd, chan, rng) */
	cfg->dsc.sbdata = malloc(cfg->dsc.sbsize);
	if (!cfg->dsc.sbdata) {
		err = -ENOMEM;
		fprintf(stderr, "cmd_write: malloc failed\n");
		goto error;
	}

	/* Get the device architecture information */
	err = a4l_fill_desc(&cfg->dsc);
	if (err < 0) {
		fprintf(stderr, "cmd_write: a4l_get_desc failed (err=%d)\n", err);
		goto error;
	}

	return 0;

error:
	if  (cfg->buffer)
		free(cfg->buffer);

	if (cfg->dsc.sbdata)
		free(cfg->dsc.sbdata);

	cfg->buffer = NULL;
	cfg->dsc.sbdata = NULL;

	return err;
}

static int init_chans_config(struct config *cfg)
{
	int err = 0;
	int len, offset;
	char *str_chans = cfg->str_chans;

	/* Recover the number of arguments */
	do {
		cfg->chans_count++;
		len = strlen(str_chans);
		offset = strcspn(str_chans, ",");
		str_chans += offset + 1;
	} while (len != offset);

	cfg->chans = malloc(cfg->chans_count * sizeof(int));
	if (!cfg->chans) {
		fprintf(stderr, "cmd_write: basic allocation failed\n");
		err = -ENOMEM;
		goto error;
	}

	/* rewind and start again. */
	str_chans = cfg->str_chans;
	cfg->chans_count = 0;

	/* ...we recover the channels */
	do {
		cfg->chans_count++;
		len = strlen(str_chans);
		offset = strcspn(str_chans, ",");
		if (sscanf(str_chans, "%u", &cfg->chans[cfg->chans_count - 1]) == 0) {
			fprintf(stderr, "cmd_write: bad channels argument\n");
			err = -EINVAL;
			goto error;
		}
		str_chans += offset + 1;
	} while (len != offset);

	/* We consider in this program that all the channels are
	   identical so we took a pointer to a chinfo structure for only
	   one of them */
	err = a4l_get_chinfo(&cfg->dsc, cfg->subd, cfg->chans[0], &cfg->cinfo);
	if (err < 0) {
		fprintf(stderr, "cmd_write: channel info recovery failed (err=%d)\n",
			err);
		goto error;
	}

	return 0;

error:
	if (cfg->chans)
		free(cfg->chans);

	cfg->chans = NULL;

	fprintf(stderr, "cmd_write: leaving %s in error\n", __FUNCTION__);

	return err;
}

static int init_range_config(struct config *cfg)
{
	int index = 0, err = 0;
	int len, offset;
	int limits[2];
	unsigned long unit;
	char * str_ranges = cfg->str_ranges;

	/* Convert min and max values */
	do {
		len = strlen(str_ranges);
		offset = strcspn(str_ranges, ",");
		if (sscanf(str_ranges, "%d", &limits[index++]) == 0) {
			err = -EINVAL;
			fprintf(stderr, "cmd_write: bad range min/max value\n");
			goto out;
		}
		str_ranges += offset + 1;
	} while (len != offset && index < 2);

	/* Find the unit among Volt, Ampere, external or no unit */
	if (!strcmp(str_ranges, "V"))
		unit = A4L_RNG_VOLT_UNIT;
	else if (!strcmp(str_ranges, "mA"))
		unit = A4L_RNG_MAMP_UNIT;
	else if (!strcmp(str_ranges, "ext"))
		unit = A4L_RNG_EXT_UNIT;
	else if (!strlen(str_ranges))
		unit = A4L_RNG_NO_UNIT;
	else {
		err = -EINVAL;
		fprintf(stderr, "cmd_write: bad range unit value\n");
		goto out;
	}

	err = a4l_find_range(&cfg->dsc,
				    cfg->subd,
				    cfg->chans[0],
				    unit, limits[0], limits[1], &cfg->rinfo);
	if (err < 0) {
		fprintf(stderr,
			"cmd_write: no range found for %s\n", cfg->str_ranges);
	} else
		err = 0;

out:
	return err;
}

static void print_config(struct config *cfg)
{
	printf("cmd_write configuration:\n");
	printf("\tRTDM device name: %s\n", cfg->filename);
	printf("\tSubdevice index: %d\n", cfg->subd);
	printf("\tSelected channels: %s\n", cfg->str_chans);
	printf("\tSelected range: %s\n", cfg->str_ranges);
	printf("\tScans count: %lu\n", cfg->scans_count);
	printf("\tWake count: %lu\n", cfg->wake_count);
}

static void cleanup_config(struct config *cfg)
{
	if (cfg->buffer) {
		free(cfg->buffer);
		cfg->buffer = NULL;
	}

	if (cfg->dsc.sbdata) {
		free(cfg->dsc.sbdata);
		cfg->dsc.sbdata = NULL;
	}

	if (cfg->dsc.fd != -1) {
		a4l_close(&cfg->dsc);
		cfg->dsc.fd = -1;
	}
}

static int init_config(struct config *cfg, int argc, char *argv[])
{
	int scan_size, err = 0;
	FILE *ifd = NULL;

	memset(cfg, 0, sizeof(struct config));
	cfg->str_chans = "0,1";
	cfg->str_ranges = "0,5,V";
	cfg->filename = "analogy0";
	cfg->dsc.fd = -1;

	while ((err = getopt_long(argc,
				  argv,
				  "vd:s:S:c:R:k:i:h", options, NULL)) >= 0) {
		switch (err) {
		case 'v':
			cfg->verbose = 1;
			break;
		case 'd':
			cfg->filename = optarg;
			break;
		case 's':
			cfg->subd = strtoul(optarg, NULL, 0);
			break;
		case 'S':
			cfg->scans_count = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			cfg->str_chans = optarg;
			break;
		case 'R':
			cfg->str_ranges = optarg;
			break;
		case 'k':
			cfg->wake_count = strtoul(optarg, NULL, 0);
			break;
		case 'i':
			ifd = fopen(optarg, "r");
			if (!ifd)
				fprintf(stderr, "cmd_write: cant open input file \n");
			break;
		case 'h':
		default:
			print_usage();
			return -EINVAL;
		};
	}

	cfg->input = ifd ? ifd : stdin;

	/* Open the analogy device and retrieve pointers on the info
	   structures */
	err = init_dsc_config(cfg);
	if (err < 0)
		goto out;

	/* Parse the channel option so as to know which and how many
	   channels will be used */
	err = init_chans_config(cfg);
	if (err < 0)
		goto out;

	/* Find out the most suitable range for the acquisition */
	err = init_range_config(cfg);
	if (err < 0)
		goto out;

	/* Compute the width of a scan */
	scan_size = cfg->chans_count * a4l_sizeof_chan(cfg->cinfo);
	if (scan_size < 0) {
		fprintf(stderr, "cmd_write: a4l_sizeof_chan failed (err=%d)\n", err);
		goto out;
	}

	/* Allocate a temporary buffer
	   TODO: implement mmap */
	cfg->buffer = malloc(BUFFER_DEPTH * scan_size);
	if (!cfg->buffer) {
		fprintf(stderr, "cmd_write: malloc failed\n");
		err = -ENOMEM;
		goto out;
	}

	/* If stdin is a terminal, we can't read binary data from it */
	if (isatty(fileno(cfg->input))) {
		memset(cfg->buffer, 0, BUFFER_DEPTH * scan_size);
		cfg->input = NULL;
	}

out:
	if (err < 0)
		cleanup_config(cfg);

	return err;
}

/* --- Input management part --- */

static int process_input(struct config *cfg, int *elements)
{
	int err = 0, filled = 0;

	/* The return value of a4l_sizeof_chan() was already
	controlled in init_config so no need to do it twice */
	int chan_size = a4l_sizeof_chan(cfg->cinfo);
	int scan_size = cfg->chans_count * chan_size;

	while (filled < BUFFER_DEPTH) {
		int i;
		double value;
		char tmp[128];

		/* stdin data are supposed to be double values from wf_generate  */
		err = fread(&value, sizeof(double), 1, cfg->input);
		if (err != 1 && !feof(cfg->input)) {
			err = -errno;
			fprintf(stderr,
				"cmd_write: stdin IO error (err=%d)\n", err);
			goto out;
		}
		else if (err == 0 && feof(cfg->input))
			goto out;

		/* the data is just for one channel */
		err = a4l_dtoraw(cfg->cinfo, cfg->rinfo, tmp, &value, 1);
		if (err < 0) {
			fprintf(stderr,
				"cmd_write: conversion "
				"from stdin failed (err=%d)\n", err);
			goto out;
		}

		/* so we have to duplicate the conversion if many
		   channels are selected for the acquisition */
		for (i = 0; i < cfg->chans_count; i++)
			memcpy(cfg->buffer + filled * scan_size + i * chan_size,
			       tmp, chan_size);
		filled ++;
	}
out:
        if (err < 0)
		return err;

        *elements = filled;

	fprintf(stderr, "cmd_write: converted %d doubles [each on %d bytes] \n",
		filled, chan_size);

	return 0;
}

/* --- Acquisition related stuff --- */
static int run_acquisition(struct config *cfg)
{
	int err = 0, elements = BUFFER_DEPTH;

	/* The return value of a4l_sizeof_chan() was already
	controlled in init_config so no need to do it twice */
	int chan_size = a4l_sizeof_chan(cfg->cinfo);
	int scan_size = cfg->chans_count * chan_size;

	if (cfg->input) {
		err = process_input(cfg, &elements);
		if (err < 0)
			return err;
		if (elements == 0)
			return -ENOENT;
	}

	fprintf(stderr, "cmd_write: write %d elements [%d bytes per element] on "
			"%d channels \n", elements, chan_size, cfg->chans_count );

	/* write data to the asynchronous buffer */
	err = a4l_async_write(&cfg->dsc, cfg->buffer, elements * scan_size,
		              A4L_INFINITE);
	if (err < 0) {
		fprintf(stderr, "cmd_write: a4l_async_write failed (%d) \n", err );
		return err;
	}

	return 0;
}

static int init_acquisition(struct config *cfg)
{
	int err = 0;

	a4l_cmd_t cmd = {
		.idx_subd = cfg->subd,
		.flags = 0,
		.start_src = TRIG_INT,
		.start_arg = 0,
		.scan_begin_src = TRIG_TIMER,
		.scan_begin_arg = 2000000, /* in ns */
		.convert_src = TRIG_NOW,
		.convert_arg = 0,
		.scan_end_src = TRIG_COUNT,
		.scan_end_arg = cfg->chans_count,
		.stop_src = cfg->scans_count ? TRIG_COUNT : TRIG_NONE,
		.stop_arg = cfg->scans_count,
		.nb_chan = cfg->chans_count,
		.chan_descs = cfg->chans
	};

	a4l_insn_t insn = {
		.type = A4L_INSN_INTTRIG,
		.idx_subd = cfg->subd,
		.data_size = 0,
	};

	/* Cancel any former command which might be in progress */
	a4l_snd_cancel(&cfg->dsc, cfg->subd);

	err = a4l_set_wakesize(&cfg->dsc, cfg->wake_count);
	if (err < 0) {
		fprintf(stderr,"cmd_read: a4l_set_wakesize failed (ret=%d)\n", err);
		goto out;
	}

	/* Send the command so as to initialize the asynchronous acquisition */
	err = a4l_snd_command(&cfg->dsc, &cmd);
	if (err < 0) {
		fprintf(stderr, "cmd_write: a4l_snd_command failed (err=%d)\n", err);
		goto out;
	}

	/* Fill the asynchronous buffer with data */
	err = run_acquisition(cfg);
	if (err < 0)
		goto out;

	/* ...before triggering the start of the output device feeding	*/
	err = a4l_snd_insn(&cfg->dsc, &insn);

out:
	return err;
}

int main(int argc, char *argv[])
{
	int err = 0;
	struct config cfg;

	err = init_config(&cfg, argc, argv);
	if (err < 0)
		goto out;

	if (cfg.verbose)
		print_config(&cfg);

	err = init_acquisition(&cfg);
	if (err < 0)
		goto out;

	while ((err = run_acquisition(&cfg)) == 0);

	err = (err == -ENOENT) ? 0 : err;

	sleep(1);

out:
	cleanup_config(&cfg);

	return err < 0 ? 1 : 0;
}
