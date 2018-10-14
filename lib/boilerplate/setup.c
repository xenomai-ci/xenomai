/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <sched.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <memory.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <xeno_config.h>
#include <boilerplate/lock.h>
#include <boilerplate/debug.h>
#include <boilerplate/ancillaries.h>
#include <xenomai/init.h>

struct base_setup_data __base_setup_data = {
	.no_sanity = !CONFIG_XENO_SANITY,
	.verbosity_level = 1,
	.trace_level = 0,
	.arg0 = NULL,
	.no_mlock = 0,
};

pid_t __node_id = 0;

int __config_done = 0;

const int __weak xenomai_auto_bootstrap = 0;

static int base_init_done;

static int main_init_done;

static DEFINE_PRIVATE_LIST(setup_list);

static const struct option base_options[] = {
	{
#define help_opt	0
		.name = "help",
	},
	{
#define affinity_opt	1
		.name = "cpu-affinity",
		.has_arg = required_argument,
	},
	{
#define verbose_opt	2
		.name = "verbose",
		.has_arg = optional_argument,
	},
	{
#define silent_opt	3
		.name = "silent",
		.has_arg = no_argument,
		.flag = &__base_setup_data.verbosity_level,
		.val = 0,
	},
	{
#define quiet_opt	4
		.name = "quiet",
		.has_arg = no_argument,
		.flag = &__base_setup_data.verbosity_level,
		.val = 0,
	},
	{
#define version_opt	5
		.name = "version",
		.has_arg = no_argument,
	},
	{
#define dumpconfig_opt	6
		.name = "dump-config",
		.has_arg = no_argument,
	},
	{
#define no_sanity_opt	7
		.name = "no-sanity",
		.has_arg = no_argument,
		.flag = &__base_setup_data.no_sanity,
		.val = 1
	},
	{
#define sanity_opt	8
		.name = "sanity",
		.has_arg = no_argument,
		.flag = &__base_setup_data.no_sanity,
	},
	{
#define trace_opt	9
		.name = "trace",
		.has_arg = optional_argument,
	},
	{
#define no_mlock_opt	10
#ifdef CONFIG_XENO_MERCURY
		.name = "no-mlock",
		.has_arg = no_argument,
		.flag = &__base_setup_data.no_mlock,
		.val = 1
#endif
	},
	{ /* Sentinel */ }
};

void __weak application_version(void)
{
	/*
	 * Applications can implement this hook for dumping their own
	 * version stamp.
	 */
}

static inline void print_version(void)
{
	application_version();
	fprintf(stderr, "based on %s\n", xenomai_version_string);
}

static inline void dump_configuration(void)
{
	int n;

	print_version();

	for (n = 0; config_strings[n]; n++)
		puts(config_strings[n]);

	printf("PTHREAD_STACK_DEFAULT=%d\n", PTHREAD_STACK_DEFAULT);
	printf("AUTOMATIC_BOOTSTRAP=%d\n", xenomai_auto_bootstrap);
}

static inline int resolve_cpuid(const char *s)
{
	return isdigit(*s) ? atoi(s) : -1;
}

static int collect_cpu_affinity(const char *cpu_list)
{
	char *s, *n, *range, *range_p = NULL, *id, *id_r;
	int start, end, cpu, nr_cpus, ret;

	/*
	 * We don't know which CPUs are online yet, but we may know
	 * which CPU identifier range is valid. Ask for the number of
	 * processors configured to find out.
	 */
	nr_cpus = (int)sysconf(_SC_NPROCESSORS_CONF);
	if (nr_cpus < 0) {
		ret = -errno;
		warning("sysconf(_SC_NPROCESSORS_CONF) failed [%s]", symerror(ret));
		return ret;
	}

	CPU_ZERO(&__base_setup_data.cpu_affinity);

	s = n = strdup(cpu_list);
	while ((range = strtok_r(n, ",", &range_p)) != NULL) {
		if (*range == '\0')
			continue;
		end = -1;
		if (range[strlen(range)-1] == '-')
			end = nr_cpus - 1;
		id = strtok_r(range, "-", &id_r);
		if (id) {
			start = resolve_cpuid(id);
			if (*range == '-') {
				end = start;
				start = 0;
			}
			id = strtok_r(NULL, "-", &id_r);
			if (id)
				end = resolve_cpuid(id);
			else if (end < 0)
				end = start;
			if (start < 0 || start >= nr_cpus ||
			    end < 0 || end >= nr_cpus)
				goto fail;
		} else {
			start = 0;
			end = nr_cpus - 1;
		}
		for (cpu = start; cpu <= end; cpu++)
			CPU_SET(cpu, &__base_setup_data.cpu_affinity);
		n = NULL;
	}

	free(s);

	/*
	 * Check we may use this affinity, at least one CPU from the
	 * given set should be available for running threads. Since
	 * CPU affinity will be inherited by children threads, we only
	 * have to set it here.
	 *
	 * NOTE: we don't clear __base_setup_data.cpu_affinity on
	 * entry to this routine to allow cumulative --cpu-affinity
	 * options to appear in the command line arguments.
	 */
	ret = sched_setaffinity(0, sizeof(__base_setup_data.cpu_affinity),
				&__base_setup_data.cpu_affinity);
	if (ret) {
		ret = -errno;
		early_warning("invalid CPU in '%s'", cpu_list);
		return ret;
	}

	return 0;
fail:
	warning("invalid CPU number/range in '%s'", cpu_list);
	free(s);

	return -EINVAL;
}

static void retrieve_default_cpu_affinity(void)
{
	CPU_ZERO(&__base_setup_data.cpu_affinity);

#ifdef CONFIG_XENO_COBALT
	/*
	 * If the Cobalt core has restricted the CPU set, update our
	 * mask accordingly.
	 */
	unsigned long cpumask;
	FILE *fp;
	int cpu;

	fp = fopen("/proc/xenomai/affinity", "r");
	if (fp == NULL)
		return;	/* uhh? */

	if (fscanf(fp, "%lx", &cpumask) == 1) {
		for (cpu = 0; cpumask; cpumask >>= 1, cpu++)
			if (cpumask & 1)
				CPU_SET(cpu, &__base_setup_data.cpu_affinity);
	}

	fclose(fp);
#endif
}

static inline char **prep_args(int argc, char *const argv[])
{
	char **uargv;
	int n;

	uargv = malloc((argc + 1) * sizeof(char *));
	if (uargv == NULL)
		return NULL;

	for (n = 0; n < argc; n++) {
		uargv[n] = strdup(argv[n]);
		if (uargv[n] == NULL)
			return NULL;
	}

	uargv[argc] = NULL;

	return uargv;
}

static inline void pack_args(int *argcp, char **argv)
{
	int in, out;

	for (in = out = 0; in < *argcp; in++) {
		if (*argv[in])
			argv[out++] = argv[in];
		else
			free(argv[in]);
	}

	argv[out] = NULL;
	*argcp = out;
}

static struct option *build_option_array(int *base_opt_startp)
{
	struct setup_descriptor *setup;
	struct option *options, *q;
	const struct option *p;
	int nopts;

	nopts = sizeof(base_options) / sizeof(base_options[0]);

	if (!pvlist_empty(&setup_list)) {
		pvlist_for_each_entry(setup, &setup_list, __reserved.next) {
			p = setup->options;
			if (p) {
				while (p->name) {
					nopts++;
					p++;
				}
			}
		}
	}

	options = malloc(sizeof(*options) * nopts);
	if (options == NULL)
		return NULL;

	q = options;

	if (!pvlist_empty(&setup_list)) {
		pvlist_for_each_entry(setup, &setup_list, __reserved.next) {
			p = setup->options;
			if (p) {
				setup->__reserved.opt_start = q - options;
				while (p->name)
					memcpy(q++, p++, sizeof(*q));
			}
			setup->__reserved.opt_end = q - options;
		}
	}

	*base_opt_startp = q - options;
	memcpy(q, base_options, sizeof(base_options));

	return options;
}

void __weak application_usage(void)
{
	/*
	 * Applications can implement this hook for dumping their own
	 * help strings.
	 */
        fprintf(stderr, "usage: %s <options>:\n", get_program_name());
}

void xenomai_usage(void)
{
	struct setup_descriptor *setup;

	print_version();

	/*
	 * Dump help strings from the highest level code to the
	 * lowest.
	 */
	application_usage();

	if (!pvlist_empty(&setup_list)) {
		pvlist_for_each_entry_reverse(setup, &setup_list,
					      __reserved.next) {
			if (setup->help)
				setup->help();
		}
	}

        fprintf(stderr, "--cpu-affinity=<cpu[,cpu]...>	set CPU affinity of threads\n");
        fprintf(stderr, "--[no-]sanity			disable/enable sanity checks\n");
        fprintf(stderr, "--verbose[=level] 		set verbosity to desired level [=1]\n");
        fprintf(stderr, "--silent, --quiet 		same as --verbose=0\n");
        fprintf(stderr, "--trace[=level] 		set tracing to desired level [=1]\n");
        fprintf(stderr, "--version			get version information\n");
        fprintf(stderr, "--dump-config			dump configuration settings\n");
#ifdef CONFIG_XENO_MERCURY
        fprintf(stderr, "--no-mlock			do not lock memory at init\n");
#endif
        fprintf(stderr, "--help				display help\n");
}

static int parse_base_options(int *argcp, char **uargv,
			      const struct option *options,
			      int base_opt_start)
{
	int c, lindex, ret, n;

	__base_setup_data.arg0 = uargv[0];
	opterr = 0;

	/*
	 * NOTE: since we pack the argument vector on the fly while
	 * processing the options, optarg should be considered as
	 * volatile by option handlers; i.e. strdup() is required if
	 * the value has to be retained. Values from the user vector
	 * returned by xenomai_init() live in permanent memory though.
	 */

	for (;;) {
		lindex = -1;
		c = getopt_long(*argcp, uargv, "-", options, &lindex);
		if (c == EOF)
			break;
		if (lindex == -1)
			continue;

		switch (lindex - base_opt_start) {
		case affinity_opt:
			ret = collect_cpu_affinity(optarg);
			if (ret)
				return ret;
			break;
		case verbose_opt:
			__base_setup_data.verbosity_level = 1;
			if (optarg)
				__base_setup_data.verbosity_level = atoi(optarg);
			break;
		case trace_opt:
			__base_setup_data.trace_level = 1;
			if (optarg)
				__base_setup_data.trace_level = atoi(optarg);
			break;
		case silent_opt:
		case quiet_opt:
		case no_mlock_opt:
		case no_sanity_opt:
		case sanity_opt:
			break;
		case version_opt:
			print_version();
			exit(0);
		case dumpconfig_opt:
			dump_configuration();
			exit(0);
		case help_opt:
			xenomai_usage();
			exit(0);
		default:
			/* Skin option, don't process yet. */
			continue;
		}

		/*
		 * Clear the first byte of the base option we found
		 * (including any companion argument), pack_args()
		 * will expunge all options we have already handled.
		 *
		 * NOTE: only options with double-dash prefix may have
		 * been recognized by getopt_long() as Xenomai
		 * ones. This reserves short options to the
		 * application layer, sharing only the long option
		 * namespace with the Xenomai core libs. In addition,
		 * the user can delimit the start of the application
		 * arguments, preceeding them by the '--' separator on
		 * the command line.
		 */
		n = optind - 1;
		if (uargv[n][0] != '-' || uargv[n][1] != '-')
			/* Clear the separate argument value. */
			uargv[n--][0] = '\0';
		uargv[n][0] = '\0'; /* Clear the option switch. */
	}

	pack_args(argcp, uargv);

	optind = 0;

	return 0;
}

static int parse_setup_options(int *argcp, char **uargv,
			       const struct option *options)
{
	struct setup_descriptor *setup;
	int lindex, n, c, ret;

	for (;;) {
		lindex = -1;
		/*
		 * We want to keep the original order of parameters in
		 * the vector, disable getopt's parameter shuffling.
		 */
		c = getopt_long(*argcp, uargv, "-", options, &lindex);
		if (c == EOF)
			break;
		if (lindex == -1)
			continue; /* Not handled here. */
		pvlist_for_each_entry(setup, &setup_list, __reserved.next) {
			if (setup->__reserved.done ||
			    setup->parse_option == NULL)
				continue;
			if (lindex < setup->__reserved.opt_start ||
			    lindex >= setup->__reserved.opt_end)
				continue;
			lindex -= setup->__reserved.opt_start;
			trace_me("%s->parse_options()", setup->name);
			ret = setup->parse_option(lindex, optarg);
			if (ret == 0)
				break;
			return ret;
		}
		n = optind - 1;
		if (uargv[n][0] != '-' || uargv[n][1] != '-')
			/* Clear the separate argument value. */
			uargv[n--][0] = '\0';
		uargv[n][0] = '\0'; /* Clear the option switch. */
	}

	pack_args(argcp, uargv);

	optind = 0;

	return 0;
}

static void __xenomai_init(int *argcp, char *const **argvp, const char *me)
{
	struct setup_descriptor *setup;
	int ret, base_opt_start;
	struct option *options;
	struct service svc;
	char **uargv;

	/*
	 * Build the global option array, merging all option sets.
	 */
	options = build_option_array(&base_opt_start);
	if (options == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	/*
	 * Prepare a user argument vector we can modify, copying the
	 * one we have been given by the bootstrap module. This vector
	 * will be expunged from Xenomai's base options as we discover
	 * them.
	 */
	uargv = prep_args(*argcp, *argvp);
	if (uargv == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	if (base_init_done) {
		trace_me("warm init from %s", me);
		goto setup;
	}

	/* Our node id. is the tid of the main thread. */
	__node_id = get_thread_pid();

	/* No ifs, no buts: we must be called over the main thread. */
	assert(getpid() == __node_id);

	/* Retrieve the default CPU affinity. */
	retrieve_default_cpu_affinity();

	/*
	 * Parse the base options first, to bootstrap the core with
	 * the right config values.
	 */
	ret = parse_base_options(argcp, uargv,
				 options, base_opt_start);
	if (ret)
		goto fail;

	trace_me("cold init from %s", me);
	
#ifndef CONFIG_SMP
	if (__base_setup_data.no_sanity == 0) {
		ret = get_static_cpu_count();
		if (ret > 0)
			early_panic("running non-SMP libraries on SMP kernel?\n"
	    "              build with --enable-smp or disable check with --no-sanity");
	}
#endif

#ifdef CONFIG_XENO_MERCURY
	if (__base_setup_data.no_mlock == 0) {
		ret = mlockall(MCL_CURRENT | MCL_FUTURE);
		if (ret) {
			ret = -errno;
			early_warning("failed to lock memory");
			goto fail;
		}
		trace_me("memory locked");
	} else
		trace_me("memory NOT locked");
#endif

	/*
	 * Now that we have bootstrapped the core, we may call the
	 * setup handlers for tuning the configuration, then parsing
	 * their own options, and eventually doing the init chores.
	 */
setup:
	if (!pvlist_empty(&setup_list)) {

		CANCEL_DEFER(svc);

		pvlist_for_each_entry(setup, &setup_list, __reserved.next) {
			if (!setup->__reserved.done && setup->tune) {
				trace_me("%s->tune()", setup->name);
				ret = setup->tune();
				if (ret)
					break;
			}
		}
		
		ret = parse_setup_options(argcp, uargv, options);
		if (ret)
			goto fail;

		/*
		 * From now on, we may not assign configuration
		 * tunables anymore.
		 */
		__config_done = 1;
	
		pvlist_for_each_entry(setup, &setup_list, __reserved.next) {
			if (setup->__reserved.done)
				continue;
			if (setup->init) {
				trace_me("%s->init()", setup->name);
				ret = setup->init();
				if (ret)
					break;
				setup->__reserved.done = 1;
			}
		}

		CANCEL_RESTORE(svc);

		if (ret) {
			early_warning("setup call %s failed", setup->name);
			goto fail;
		}
	} else
		__config_done = 1;

	free(options);

#ifdef CONFIG_XENO_DEBUG
	if (!base_init_done && __base_setup_data.verbosity_level > 0) {
		early_warning("Xenomai compiled with %s debug enabled,\n"
			"                              "
			"%shigh latencies expected [--enable-debug=%s]",
#ifdef CONFIG_XENO_DEBUG_FULL
			"full", "very ", "full"
#else
			"partial", "", "partial"
#endif
			);
	}
#endif

	/*
	 * The final user arg vector only contains options we could
	 * not handle. The caller should be able to process them, or
	 * bail out.
	 */
	*argvp = uargv;
	base_init_done = 1;

	return;
fail:
	early_panic("initialization failed, %s", symerror(ret));
}

void xenomai_init(int *argcp, char *const **argvp)
{
	const char *me = get_program_name();

	if (main_init_done) {
		early_warning("duplicate call from main program "
			      "to %s() ignored", __func__);
		early_warning("(xeno-config --no-auto-init disables implicit call)");
	}

	__xenomai_init(argcp, argvp, me);
	main_init_done = 1;
	trace_me("%s bootstrap done", me);
}

void xenomai_init_dso(int *argcp, char *const **argvp)
{
	__xenomai_init(argcp, argvp, "DSO");
	trace_me("DSO bootstrap done");
}

void __trace_me(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "--  ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
	va_end(ap);
}

void __register_setup_call(struct setup_descriptor *p, int id)
{
	struct setup_descriptor *pos;

	/*
	 * Trap late registration due to wrong constructor priorities.
	 */
	assert(!main_init_done);
	p->__reserved.id = id;
	p->__reserved.done = 0;

	/*
	 * Insert the new descriptor (highest id first).
	 */
	if (!pvlist_empty(&setup_list)) {
		pvlist_for_each_entry_reverse(pos, &setup_list, __reserved.next) {
			if (id >= pos->__reserved.id) {
				atpvh(&pos->__reserved.next, &p->__reserved.next);
				return;
			}
		}
	}
	pvlist_prepend(&p->__reserved.next, &setup_list);
}

const char *get_program_name(void)
{
	return basename(__base_setup_data.arg0 ?: "program");
}
