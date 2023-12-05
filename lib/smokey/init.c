/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fnmatch.h>
#include <boilerplate/list.h>
#include <boilerplate/ancillaries.h>
#include <smokey/smokey.h>

#include "copperplate/internal.h"

/**
 * @defgroup smokey Smokey API
 *
 * A simple infrastructure for writing and running smoke tests.
 *
 * Smokey is based on the Copperplate API, therefore is available over
 * the single and dual kernel Xenomai configurations indifferently.
 *
 * The API provides a set of services for declaring any number of test
 * plugins, embodied into a test program. Each plugin usually
 * implements a single smoke test, checking a particular feature of
 * interest. Each plugin present in the running executable is
 * automatically detected by the Smokey init routine. In addition, the
 * Smokey API parses all arguments and options passed on the command
 * line to the executable, running pre-defined actions which are
 * therefore automatically recognized by all programs linked against
 * the Smokey library.
 *
 * @par Writing smoke tests with Smokey
 *
 * A smoke test is composed of a routine which implements the test
 * code, and a set of runtime settings/attributes for running such
 * code. The routine prototype shall be:
 *
 * @code
 * int run_<test_name>(struct smokey_test *t, int argc, char *const argv[])
 * @endcode
 *
 * The test routine should return a zero value for success, or any
 * negated POSIX error code for indicating the failure to the test
 * driver (e.g. -EINVAL if some value is found to be wrong).
 *
 * With @a t referring to the Smokey test descriptor, and @a argc, @a
 * argv the argument count and vector expunged from all the inner
 * options which may have been previously interpreted by the Smokey
 * API and inner layers (such as Copperplate).
 *
 * The Smokey API provides the services to declare a complete test
 * (named @b foo in this example) as follows:
 *
 * @code
 * #include <smokey/smokey.h>
 *
 * smokey_test_plugin(foo, // test name
 *                    SMOKEY_ARGLIST( // argument list
 *			      	SMOKEY_INT(some_integer),
 *			      	SMOKEY_STRING(some_string),
 *			      	SMOKEY_BOOL(some_boolean),
 *		      ),
 *                    // description
 *		      "A dummy Smokey-based test plugin\n"
 *		      "\taccepting three optional arguments:\n"
 *		      "\tsome_integer=<value>\n"
 *		      "\tsome_string=<string>\n"
 *		      "\tsome_bool[=0/1]\n"
 * );
 *
 * static int run_foo(struct smokey_test *t, int argc, char *const argv[])
 * {
 *      int i_arg = 0, nargs;
 *      char *s_arg = NULL;
 *      bool b_arg = false;
 *
 * 	nargs = smokey_parse_args(t, argc, argv);
 *
 *	if (SMOKEY_ARG_ISSET(foo, some_integer))
 *		i_arg = SMOKEY_ARG_INT(foo, some_integer);
 *	if (SMOKEY_ARG_ISSET(foo, some_string))
 *		s_arg = SMOKEY_ARG_STRING(foo, some_string);
 *	if (SMOKEY_ARG_ISSET(foo, some_boolean))
 *		b_arg = SMOKEY_ARG_INT(foo, some_boolean);
 *
 *      return run_some_hypothetical_smoke_test_code(i_arg, s_arg, b_arg);
 * }
 * @endcode
 *
 * As illustrated, a smoke test is at least composed of a test plugin
 * descriptor (i.e. @a smokey_test_plugin()), and a run handler named
 * after the test.
 *
 * @par Test arguments
 *
 * Smokey recognizes three argument declarators, namely:
 * SMOKEY_INT(name) for a C (signed) integer, SMOKEY_BOOL(name) for a
 * boolean value and SMOKEY_STRING(name) for a character string.
 *
 * Each argument can be passed to the test code as a name=value pair,
 * where @a name should match one of the declarators.  Before the
 * test-specific arguments can be accessed, a call to
 * smokey_parse_args() must be issued by the test code, passing the
 * parameters received in the run handler. This routine returns the
 * number of arguments found on the command line matching the
 * an entry in SMOKEY_ARGLIST().
 *
 * Once smokey_parse_args() has returned with a non-zero value, each
 * argument can be checked individually for presence. If a valid
 * argument was matched on the command line,
 * SMOKEY_ARG_ISSET(test_name, arg_name) returns non-zero. In the
 * latter case, its value can be retrieved by a similar call to
 * SMOKEY_ARG_INT(test_name, arg_name), SMOKEY_ARG_STRING(test_name,
 * arg_name) or SMOKEY_ARG_BOOL(test_name, arg_name).
 *
 * In the above example, passing "some_integer=3" on the command line of
 * any program implementing such Smokey-based test would cause the
 * variable i_arg to receive "3" as a value.
 *
 * @par Pre-defined Smokey options
 *
 * Any program linked against the Smokey API implicitly recognizes the
 * following options:
 *
 * - --list[=<id[,id...]>] dumps the list of tests implemented in the
 *   program to stdout. This list may be restricted to the tests
 *   matching the optional regular expression (see --run). The
 *   information given includes the description strings provided in
 *   the plugin declarators (smokey_test_plugin()).  The position and
 *   symbolic name of each test is also issued, which may be used in
 *   id specifications with the --run option (see below).
 *
 * @note Test positions may vary depending on changes to the host
 * program like adding or removing other tests, the symbolic name
 * however is stable and identifies each test uniquely.
 *
 * - --run[=<id[,id...]>] selects the tests to be run, determining the
 *   active test list among the overall set of tests detected in the
 *   host program.  The test driver code (e.g. implementing a test
 *   harness program on top of Smokey) may then iterate over the @a
 *   smokey_test_list for accessing each active test individually, in
 *   the enumeration order specified by the user (Use
 *   for_each_smokey_test() for that).
 *
 *   If no argument is passed to --run, Smokey assumes that all tests
 *   detected in the current program should be picked, filling @a
 *   smokey_test_list with tests by increasing position order.
 *
 *   Otherwise, id may be a test position, a symbolic name, or a range
 *   thereof delimited by a dash character. A symbolic name may be
 *   matched using a glob(3) type regular expression.
 *
 *   id specification may be:
 *
 *   - 0-9, picks tests #0 to #9
 *   - -3, picks tests #0 to #3
 *   - 5-, picks tests #5 to the highest possible test position
 *   - 2-0, picks tests #2 to #0, in decreasing order
 *   - foo, picks test foo only
 *   - 0,1,foo- picks tests #0, #1, and any test from foo up to the
 *     last test defined
 *   - fo* picks any test with a name starting by "fo"
 *
 * - --exclude=<id[,id...]> excludes the given tests from the test
 *   list. The format of the argument is identical to the one accepted
 *   by the --run option.
 *
 * - --keep-going sets the boolean flag @a smokey_keep_going to a
 *   non-zero value, indicating to the test driver that receiving a
 *   failure code from a smoke test should not abort the test loop.
 *   This flag is not otherwise interpreted by the Smokey API.
 *
 * - --verbose[=level] sets the integer @a smokey_verbose_mode to a
 *   non-zero value, which should be interpreted by all parties as the
 *   desired verbosity level (defaults to 1).
 *
 * - --vm gives a hint to the test code, about running in a virtual
 * environment, such as KVM. When passed, the boolean @a smokey_on_vm
 * is set. Each test may act upon this setting, such as skipping
 * time-dependent checks that may fail due to any slowdown induced by
 * the virtualization.
 *
 * @par Writing a test driver based on the Smokey API
 *
 * A test driver provides the main() entry point, which should iterate
 * over the test list (@a smokey_test_list) prepared by the Smokey
 * API, for running each test individually.  The @a for_each_smokey_test()
 * helper is available for iterating over the active test list.
 *
 * When this entry point is called, all the initialization chores,
 * including the test detection and the active test selection have
 * been performed by the Smokey API already.
 *
 * @par Issuing information notices
 *
 * The printf-like @a smokey_note() routine is available for issuing
 * notices to the output device (currently stdout), unless --silent
 * was detected on the command line. smokey_note() outputs a
 * terminating newline character. Notes are enabled for any verbosity
 * level greater than zero.
 *
 * @par Issuing trace messages
 *
 * The printf-like @a smokey_trace() routine is available for issuing
 * progress messages to the output device (currently stdout), unless
 * --silent was detected on the command line. smokey_trace() outputs a
 * terminating newline character. Traces are enabled for any verbosity
 * level greater than one.
 *
 * Therefore, a possible implementation of a test driver could be as
 * basic as:
 *
 * @code
 * #include <stdio.h>
 * #include <error.h>
 * #include <smokey/smokey.h>
 *
 * int main(int argc, char *const argv[])
 * {
 *     struct smokey_test *t;
 *     int ret;
 *
 *     if (pvlist_empty(&smokey_test_list))
 *      	return 0;
 *
 *	for_each_smokey_test(t) {
 *		ret = t->run(t, argc, argv);
 *		if (ret) {
 *			if (smokey_keep_going)
 *				continue;
 *			error(1, -ret, "test %s failed", t->name);
 *		}
 *		smokey_note("%s OK", t->name);
 *	}
 *
 *	return 0;
 * }
 * @endcode
 */

DEFINE_PRIVATE_LIST(smokey_test_list);

int smokey_keep_going;

int smokey_verbose_mode = 1;

int smokey_on_vm = 0;

static DEFINE_PRIVATE_LIST(register_list);

static DEFINE_PRIVATE_LIST(exclude_list);

static char *include_arg;

static char *exclude_arg;

static int test_count;

static int do_list;

static int do_run;

static const struct option smokey_options[] = {
	{
#define keep_going_opt	0
		.name = "keep-going",
		.has_arg = no_argument,
		.flag = &smokey_keep_going,
		.val = 1,
	},
	{
#define run_opt		1
		.name = "run",
		.has_arg = optional_argument,
		.flag = &do_run,
		.val = 1,
	},
	{
#define list_opt	2
		.name = "list",
		.has_arg = optional_argument,
		.flag = &do_list,
		.val = 1,
	},
	{
#define vm_opt		3
		.name = "vm",
		.has_arg = no_argument,
		.flag = &smokey_on_vm,
		.val = 1,
	},
	{
#define exclude_opt	4
		.name = "exclude",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
};

static void smokey_help(void)
{
        fprintf(stderr, "--keep-going			don't stop upon test error\n");
	fprintf(stderr, "--list[=<id[,id...]>]]		list [matching] tests\n");
	fprintf(stderr, "--run[=<id[,id...]>]]		run [portion of] the test list\n");
	fprintf(stderr, "--exclude=<id[,id...]>]	exclude test(s) from the run list\n");
	fprintf(stderr, "--vm				hint about running in a virtual environment\n");
}

static void pick_test_range(struct pvlistobj *dst, int start, int end)
{
	struct smokey_test *t, *tmp;

	/* Pick tests in the suggested range order. */

	if (start <= end) {
		pvlist_for_each_entry_safe(t, tmp, &register_list, __reserved.next) {
			if (t->__reserved.id >= start &&
			    t->__reserved.id <= end) {
				pvlist_remove(&t->__reserved.next);
				pvlist_append(&t->__reserved.next, dst);
			}
		}
	} else {
		pvlist_for_each_entry_reverse_safe(t, tmp, &register_list, __reserved.next) {
			if (t->__reserved.id >= end &&
			    t->__reserved.id <= start) {
				pvlist_remove(&t->__reserved.next);
				pvlist_append(&t->__reserved.next, dst);
			}
		}
	} 
}

static void drop_test_range(struct pvlistobj *dst, int start, int end)
{
	struct smokey_test *t, *tmp;

	/*
	 * Drop tests from the register list so that we won't find
	 * them when applying the inclusion filter next, order is not
	 * significant.
	 */
	pvlist_for_each_entry_safe(t, tmp, &register_list, __reserved.next) {
		if (t->__reserved.id >= start &&
		    t->__reserved.id <= end) {
			pvlist_remove(&t->__reserved.next);
			pvlist_append(&t->__reserved.next, dst);
		}
	}
}

static int resolve_id(const char *s)
{
	struct smokey_test *t;

	if (isdigit(*s))
		return atoi(s);

	/*
	 * CAUTION: as we transfer items from register_list to a
	 * destination list, we may end up with an empty source list,
	 * which is a perfectly valid situation. Unlike having an
	 * empty registration list at startup, which would mean that
	 * no test is available from the current program.
	 */
	if (pvlist_empty(&register_list))
		return -1;

	pvlist_for_each_entry(t, &register_list, __reserved.next)
		if (!fnmatch(s, t->name, FNM_PATHNAME))
			return t->__reserved.id;

	return -1;
}

static int glob_match(struct pvlistobj *dst, const char *s)
{
	struct smokey_test *t, *tmp;
	int matches = 0;

	if (pvlist_empty(&register_list))
		return 0;

	pvlist_for_each_entry_safe(t, tmp, &register_list, __reserved.next) {
		if (!fnmatch(s, t->name, FNM_PATHNAME)) {
			pvlist_remove(&t->__reserved.next);
			pvlist_append(&t->__reserved.next, dst);
			matches++;
		}
	}

	return matches;
}

static int apply_test_filter(const char *test_enum,
			     struct pvlistobj *dst,
			     void (*filter_action)(struct pvlistobj *dst,
						   int start, int end))
{
	char *s = strdup(test_enum), *n, *range, *range_p = NULL, *id, *id_r;
	int start, end;

	n = s;
	while ((range = strtok_r(n, ",", &range_p)) != NULL) {
		if (*range == '\0')
			continue;
		end = -1;
		if (range[strlen(range)-1] == '-')
			end = test_count - 1;
		id = strtok_r(range, "-", &id_r);
		if (id) {
			if (glob_match(dst, id)) {
				if (strtok_r(NULL, "-", &id_r))
					goto fail;
				n = NULL;
				continue;
			}
			start = resolve_id(id);
			if (*range == '-') {
				end = start;
				start = 0;
			}
			id = strtok_r(NULL, "-", &id_r);
			if (id)
				end = resolve_id(id);
			else if (end < 0)
				end = start;
			if (start < 0 || start >= test_count ||
			    end < 0 || end >= test_count)
				goto fail;
		} else {
			start = 0;
			end = test_count - 1;
		}
		filter_action(dst, start, end);
		n = NULL;
	}

	free(s);

	return 0;
fail:
	warning("invalid test range in %s", test_enum, test_count - 1);
	free(s);

	return -EINVAL;
}

static int run_include_filter(const char *include_enum)
{
	return apply_test_filter(include_enum, &smokey_test_list,
				 pick_test_range);
}

static int run_exclude_filter(const char *exclude_enum)
{
	return apply_test_filter(exclude_enum, &exclude_list,
				 drop_test_range);
}

static int list_tests(void)
{
	struct pvlistobj list;
	struct smokey_test *t;
	int ret = 0;

	pvlist_init(&list);
	
	if (include_arg) {
		ret = apply_test_filter(include_arg, &list,
				pick_test_range);
		free(include_arg);
	} else
		pick_test_range(&list, 0, test_count);
	
	if (!pvlist_empty(&list))
		pvlist_for_each_entry(t, &list, __reserved.next)
			printf("#%-3d %s\n\t%s\n",
			       t->__reserved.id, t->name, t->description);
	return ret;
}

void smokey_register_plugin(struct smokey_test *t)
{
	pvlist_append(&t->__reserved.next, &register_list);
	t->__reserved.id = test_count++;
}

static int smokey_parse_option(int optnum, const char *optarg)
{
	switch (optnum) {
	case list_opt:
	case run_opt:
		if (optarg)
			include_arg = strdup(optarg);
		break;
	case exclude_opt:
		exclude_arg = strdup(optarg);
		break;
	case keep_going_opt:
	case vm_opt:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smokey_init(void)
{
	int ret = 0;

	if (do_list)
		return list_tests();

	if (do_run) {
		if (pvlist_empty(&register_list)) {
			warning("no test registered");
			return -EINVAL;
		}

		if (exclude_arg) {
			run_exclude_filter(exclude_arg);
			free(exclude_arg);
		}

		if (include_arg) {
			ret = run_include_filter(include_arg);
			free(include_arg);
		} else
			pick_test_range(&smokey_test_list, 0, test_count);
	
		if (pvlist_empty(&smokey_test_list)) {
			warning("no test selected");
			ret = -EINVAL;
		}
	}

	if (pvlist_empty(&smokey_test_list))
		set_runtime_tunable(verbosity_level, 0);
	else
		smokey_verbose_mode = get_runtime_tunable(verbosity_level);

	return ret;
}

static struct setup_descriptor smokey_interface = {
	.name = "smokey",
	.init = smokey_init,
	.options = smokey_options,
	.parse_option = smokey_parse_option,
	.help = smokey_help,
};

post_setup_call(smokey_interface);
