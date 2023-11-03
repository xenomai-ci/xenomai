/*
 * Analogy for Linux, configuration program
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
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <xeno_config.h>
#include <rtdm/analogy.h>

#define ANALOGY_DRIVERS_PROC "/proc/analogy/drivers"
#define ANALOGY_DEVICES_PROC "/proc/analogy/devices"

#define __OPTS_DELIMITER ","

enum actions {
	DO_ATTACH = 0x1,
	DO_DETACH = 0x2,
	DO_BUFCONFIG = 0x4,
};

/* Declare prog variables */
int vlevel = 1;
enum actions actions = 0;
int bufsize = -1;
struct option a4l_conf_opts[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"quiet", no_argument, NULL, 'q'},
	{"version", no_argument, NULL, 'V'},
	{"remove", no_argument, NULL, 'r'},
	{"read-buffer-size", required_argument, NULL, 'R'},
	{"write-buffer-size", required_argument, NULL, 'W'},
	{"buffer-size", required_argument, NULL, 'S'},
	{0},
};

/* Misc functions */
static void print_version(void)
{
	fprintf(stdout, "analogy_config: version %s\n", PACKAGE_VERSION);
}

static void print_usage(void)
{
	fprintf(stdout, "usage:\tanalogy_config [OPTS] devfile driver "
			"<driver specific options>"
		        "- ex: [OPTS] analogy0 analogy_fake 0x378,7,18 \n");
	fprintf(stdout, "\tOPTS:\t -v, --verbose: verbose output\n");
	fprintf(stdout, "\t\t -q, --quiet: quiet output\n");
	fprintf(stdout, "\t\t -V, --version: print program version\n");
	fprintf(stdout, "\t\t -r, --remove: detach a device\n");
	fprintf(stdout, "\t\t -S, --buffer-size: set default size in kB\n");
	fprintf(stdout, "\tDeprecated options:\n");
	fprintf(stdout, "\t\t -R, --read-buffer-size: read buffer size in kB\n");
	fprintf(stdout, "\t\t -W, --write-buffer-size: write buffer size in kB\n");
}

static int parse_extra_arg(char const *opts, a4l_lnkdesc_t *lnkdsc)
{
	int i, err, cnt, len, ofs;
	unsigned long *p;
	char const *q;

	/* count the numer of driver specific comma separated arguments */
	q = opts;
	cnt = 1;
	while ((q = strstr(q, __OPTS_DELIMITER)) != NULL) {
		   q += strlen(__OPTS_DELIMITER);
		   cnt++;
	}

	/* alloc memory for the individual params converted to unsigned long */
	len = cnt * sizeof(unsigned long);
	p = malloc(len);
	if (!p) {
		fprintf(stderr, "analogy_config: memory allocation failed\n");
		err = -ENOMEM;
		goto out;
	}

	lnkdsc->opts = (void *)p;
	lnkdsc->opts_size = len;

	/* We set errno to 0 so as to be sure that strtoul did not fail */
	errno = 0;
	i = 0;
	do {
		len = strlen(opts);
		ofs = strcspn(opts, __OPTS_DELIMITER);
		p[i] = strtoul(opts, NULL, 0);
		if (errno != 0) {
			err = -errno;
			goto fail;
		}
		opts += ofs + 1;
		i++;
	} while (len != ofs);

	return 0;

fail:
        free(p);
out:
	lnkdsc->opts  = NULL;
	lnkdsc->opts_size = 0;

	return err;
}

static inline int do_detach(int fd, char *devfile)
{
	int err;

	err = a4l_sys_detach(fd);
	if (err < 0)
		fprintf(stderr,"analogy_config: a4l_detach(%s) failed err=%d\n",
			devfile, err);
	return err;
}

static inline int do_attach(int fd, int argc, char *argv[], int optind)
{
	a4l_lnkdesc_t lnkdsc;
	int err;

	memset(&lnkdsc, 0, sizeof(a4l_lnkdesc_t));
	lnkdsc.bname = argv[optind + 1];
	lnkdsc.bname_size = strlen(argv[optind + 1]);

	/* Process driver specific options if any */
	if (argc - optind == 3) {
		err = parse_extra_arg(argv[optind + 2], &lnkdsc);
		if (err < 0) {
			fprintf(stderr, "analogy_config: "
					"driver specific options failed\n");
			fprintf(stderr, "\twarning: driver specific "
					"options must be integers \n");
			print_usage();
			return err;
		}
	}

	err = a4l_sys_attach(fd, &lnkdsc);
	if (err < 0)
		fprintf(stderr, "analogy_config: a4l_attach(%s) failed err=%d\n",
			lnkdsc.bname, err);

	if (lnkdsc.opts != NULL)
		free(lnkdsc.opts);

	return err;
}

static inline int do_bufcfg(int fd, char *devfile, int bufsize)
{
	int err;
	/*
	 * inform the driver of the size of the buffer it will need to
	 * allocate at opening.
	 */
	err = a4l_sys_bufcfg(fd, A4L_BUF_DEFMAGIC, bufsize);
	if (err < 0) {
		fprintf(stderr,
			"analogy_config: a4l_bufcfg(%s) configuration failed "
			"err=%d\n", devfile, err);
	}

	return err;
}

static inline int check_params(enum actions *actions, const int argc, int optind)
{
	/* Here we have choice:
	 *  - if the option -r is set, only one additional option is useful
	 *  - if the option -S is set without no attach options
	 *  - if the option -S is set with attach options
	 */

	if ((*actions & DO_DETACH) && argc - optind < 1 ) {
		fprintf(stderr, "analogy_config: specify a device to detach\n");
		return -EINVAL;
	}

	if ((*actions & DO_DETACH) && (*actions & DO_BUFCONFIG)) {
		fprintf(stderr,
			"analogy_config: skipping buffer size configuration"
			"because of detach action\n");
	}

	if (!(*actions & DO_DETACH) &&
	    !(*actions & DO_BUFCONFIG) && argc - optind < 2) {
		print_usage();
		return -EINVAL;;
	}
	else if (!(*actions & DO_DETACH) && argc - optind >= 2)
		*actions |= DO_ATTACH;

	return 0;
}

int main(int argc, char *argv[])
{
	int err = 0, fd = -1;
	char *devfile;
	int c;

	/* Compute arguments */
	while ((c =
		getopt_long(argc, argv, "hvqVrR:W:S:", a4l_conf_opts,
			    NULL)) >= 0) {
		switch (c) {
		case 'h':
			print_usage();
			goto done;
		case 'v':
			vlevel = 2;
			break;
		case 'q':
			vlevel = 0;
			break;
		case 'V':
			print_version();
			goto done;
		case 'r':
			actions |= DO_DETACH;
			break;
		case 'R':
		case 'W':
			fprintf(stdout,
				"analogy_config: the option --read-buffer-size "
				"and --write-buffer-size will be deprecated; "
				"please use --buffer-size instead (-S)\n");
		case 'S':
			actions |= DO_BUFCONFIG;
			bufsize = strtoul(optarg, NULL, 0);
			break;
		default:
			print_usage();
			goto done;
		}
	}

	err = check_params(&actions, argc, optind);
	if (err)
		goto done;

	devfile = argv[optind];

	fd = a4l_sys_open(devfile);
	if (fd < 0) {
		err = fd;
		fprintf(stderr,"analogy_config: a4l_open(%s) failed err=%d\n",
			devfile, err);
		goto done;
	}

	if (actions & DO_DETACH) {
		err = do_detach(fd, devfile);
	}
	else {
		if (actions & DO_ATTACH)
			err = do_attach(fd, argc, argv, optind);

		if (err)
			goto done;

		if (actions & DO_BUFCONFIG)
			err = do_bufcfg(fd, devfile, bufsize);
	}

done:
        if (err < 0)
		fprintf(stderr,
			"analogy_config: check the procfs information:\n"
			" - analogy devices: %s \n"
			" - analogy drivers: %s \n",
			ANALOGY_DEVICES_PROC,
			ANALOGY_DRIVERS_PROC);

	if (fd >= 0)
		a4l_sys_close(fd);

	return err;
}
