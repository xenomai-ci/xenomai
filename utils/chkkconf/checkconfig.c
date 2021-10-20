/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <search.h>
#include <error.h>
#include <errno.h>
#include <sys/utsname.h>

#define DEFAULT_KCONFIG    "/proc/config.gz"
#define DEFAULT_CHECKLIST  DATADIR"/kconf-checklist"

#define short_optlist "@hqf:L:a:H:"

static int hash_size = 16384;

static bool quiet;

static const struct option options[] = {
	{"file", required_argument, NULL, 'f'},
	{"check-list", required_argument, NULL, 'L'},
	{"arch", required_argument, NULL, 'a'},
	{"hash-size", required_argument, NULL, 'H'},
	{"quiet", no_argument, NULL, 'q'},
	{"help", no_argument, NULL, 'h'},
	{0}
};

static char *hash_config(FILE *kconfp)
{
	char buf[BUFSIZ], *sym, *val, *arch = NULL;
	ENTRY entry, *e;
	int ret;

	ret = hcreate(hash_size);
	if (!ret)
		error(1, errno, "hcreate(%d)", hash_size);

	while (fgets(buf, sizeof(buf), kconfp)) {
		if (*buf == '#') {
			sscanf(buf, "# Linux/%m[^ ]", &arch);
			continue;
		}
		ret = sscanf(buf, "%m[^=]=%ms\n", &sym, &val);
		if (ret != 2)
			continue;
		if (strncmp(sym, "CONFIG_", 7))
			continue;
		if (strcmp(val, "y") && strcmp(val, "m"))
			continue;
		entry.key = sym;
		entry.data = NULL;
		e = hsearch(entry, FIND);
		if (e)
			continue; /* uhh? */
		entry.data = val;
		if (!hsearch(entry, ENTER))
			error(1, ENOMEM, "h-table full -- try -H");
	}

	return arch;
}

static char *next_token(char **next)
{
	char *p = *next, *s;

	for (;;) {
		if (!*p) {
			*next = p;
			return strdup("");
		}
		if (!isspace(*p))
			break;
		p++;
	}

	s = p;

	if (!isalnum(*p) && *p != '_') {
		*next = p + 1;
		return strndup(s, 1);
	}

	do {
		if (!isalnum(*p) && *p != '_') {
			*next = p;
			return strndup(s, p - s);
		}
	} while (*++p);

	*next = p;

	return strdup(s);
}

static const char *get_arch_alias(const char *arch)
{
	if (!strcmp(arch, "arm64"))
		return "aarch64";

	if (!strcmp(arch, "arm"))
		return "aarch32";

	return arch;
}

static int apply_checklist(FILE *checkfp, const char *cpuarch)
{
	char buf[BUFSIZ], *token, *next, *sym, *val;
	int lineno = 0, failed = 0;
	bool not, notcond;
	const char *arch;
	ENTRY entry, *e;

	while (fgets(buf, sizeof(buf), checkfp)) {
		lineno++;
		next = buf;

		token = next_token(&next);
		if (!*token || !strcmp(token, "#")) {
			free(token);
			continue;
		}

		not = *token == '!';
		if (not) {
			free(token);
			token = next_token(&next);
		}

		sym = token;
		if (strncmp(sym, "CONFIG_", 7))
			error(1, EINVAL,
				"invalid check list symbol '%s' at line %d",
				sym, lineno);

		token = next_token(&next);
		val = NULL;
		if (*token == '=') {
			free(token);
			val = next_token(&next);
			token = next_token(&next);
		}

		if (!strcmp(token, "if")) {
			free(token);
			token = next_token(&next);
			notcond = *token == '!';
			if (notcond) {
				free(token);
				token = next_token(&next);
			}
			if (strncmp(token, "CONFIG_", 7))
				error(1, EINVAL,
					"invalid condition symbol '%s' at line %d",
					token, lineno);
			entry.key = token;
			entry.data = NULL;
			e = hsearch(entry, FIND);
			free(token);
			if (!((e && !notcond) || (!e && notcond)))
				continue;
			token = next_token(&next);
		}

		if (!strcmp(token, "on")) {
			free(token);
			token = next_token(&next);
			arch = get_arch_alias(token);
			if (strncmp(cpuarch, arch, strlen(arch))) {
				free(token);
				continue;
			}
		}

		free(token);

		entry.key = sym;
		entry.data = NULL;
		e = hsearch(entry, FIND);

		if (val && !strcmp(val, "n"))
			not = !not;

		if (e && (not || (val && strcmp(val, e->data)))) {
			if (!quiet)
				printf("%s=%s\n", sym, (const char *)e->data);
			failed++;
		} else if (!e && !not) {
			if (!quiet)
				printf("%s=n\n", sym);
			failed++;
		}

		free(sym);
		if (val)
			free(val);
	}

	return failed;
}

static void usage(char *arg0)
{
	fprintf(stderr, "usage: %s [options]:\n", basename(arg0));
	fprintf(stderr, "-f --file=<.config>     Kconfig file to check [=/proc/config.gz]\n");
	fprintf(stderr, "-L --check-list=<file>  configuration check list [="DATADIR"/kconf-checklist]\n");
	fprintf(stderr, "-a --arch=<cpuarch>     CPU architecture assumed\n");
	fprintf(stderr, "-H --hash-size=<N>      set the hash table size [=16384]\n");
	fprintf(stderr, "-q --quiet              suppress output\n");
	fprintf(stderr, "-h --help               this help\n");
}

int main(int argc, char *const argv[])
{
	const char *kconfig = DEFAULT_KCONFIG, *check_list = NULL,
		*defarch, *arch = NULL, *p;
	FILE *kconfp, *checkfp;
	struct utsname ubuf;
	int c, ret;
	char *cmd;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			break;
		case 'f':
			kconfig = optarg;
			break;
		case 'L':
			check_list = optarg;
			break;
		case 'a':
			arch = optarg;
			break;
		case 'H':
			hash_size = atoi(optarg);
			if (hash_size < 16384)
				hash_size = 16384;
			break;
		case 'q':
			quiet = true;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		case '@':
			printf("check kernel configuration\n");
			return 0;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	/*
	 * We may be given a gzipped input file. Finding gunzip on a
	 * minimalist rootfs (e.g. busybox) may be more likely than
	 * having the zlib development files available from a common
	 * cross-toolchain. So go for popen-ing gunzip on the target
	 * instead of depending on libz on the development host.
	 */
	if (!strcmp(kconfig, "-")) {
		kconfp = stdin;
	} else {
		p = strstr(kconfig, ".gz");
		if (!p || strcmp(p, ".gz")) {
			kconfp = fopen(kconfig, "r");
		} else {
			if (access(kconfig, R_OK))
				error(1, errno, "cannot access %s%s",
					kconfig,
					strcmp(kconfig, DEFAULT_KCONFIG) ? "" :
					"\n(you need CONFIG_IKCONFIG_PROC enabled)");
			ret = asprintf(&cmd, "gunzip -c %s", kconfig);
			if (ret < 0)
				error(1, ENOMEM, "asprintf()");
			kconfp = popen(cmd, "r");
			free(cmd);
		}
		if (kconfp == NULL)
			error(1, errno, "cannot open %s for reading", kconfig);
	}

	defarch = hash_config(kconfp);

	if (check_list == NULL)
		check_list = DEFAULT_CHECKLIST;

	if (access(check_list, R_OK))
		error(1, errno, "cannot access %s", check_list);

	checkfp = fopen(check_list, "r");
	if (checkfp == NULL)
		error(1, errno, "cannot open %s for reading", check_list);

	if (arch == NULL) {
		if (defarch) {
			arch = get_arch_alias(defarch);
		} else {
			ret = uname(&ubuf);
			if (ret)
				error(1, errno, "utsname()");
			arch = ubuf.machine;
		}
	} else {
		arch = get_arch_alias(arch);
	}

	return apply_checklist(checkfp, arch);
}
