/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <pwd.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/registry.h"
#include "copperplate/timerobj.h"
#include "xenomai/init.h"
#include "internal.h"

struct copperplate_setup_data __copperplate_setup_data = {
	.mem_pool = 1024 * 1024, /* Default, 1Mb. */
	.no_registry = 0,
	.registry_root = DEFAULT_REGISTRY_ROOT,
	.session_label = NULL,
	.session_root = NULL,
	.session_gid = USHRT_MAX,
};

#ifdef CONFIG_XENO_COBALT
int __cobalt_print_bufsz = 32 * 1024;
#endif

static const struct option copperplate_options[] = {
	{
#define mempool_opt	0
		.name = "mem-pool-size",
		.has_arg = required_argument,
	},
	{
#define regroot_opt	1
		.name = "registry-root",
		.has_arg = required_argument,
	},
	{
#define no_registry_opt	2
		.name = "no-registry",
		.has_arg = no_argument,
		.flag = &__copperplate_setup_data.no_registry,
		.val = 1
	},
	{
#define session_opt	3
		.name = "session",
		.has_arg = required_argument,
	},
	{
#define shared_registry_opt	4
		.name = "shared-registry",
		.has_arg = no_argument,
		.flag = &__copperplate_setup_data.shared_registry,
		.val = 1,
	},
	{ /* Sentinel */ }
};

/*
 * Routine to bring up the basic copperplate features, but not enough
 * to run over a non-POSIX real-time interface though. For internal
 * code only, such as sysregd. No code traversed should depend on
 * __copperplate_setup_data.
 */
void copperplate_bootstrap_internal(const char *arg0, char *mountpt,
				    int regflags)
{
	int ret;

	__node_id = get_thread_pid();

	CPU_ZERO(&__base_setup_data.cpu_affinity);

	__boilerplate_init();

	ret = heapobj_pkg_init_private();
	if (ret) {
		early_warning("failed to initialize main private heap");
		goto fail;
	}

	ret = __registry_pkg_init(arg0, mountpt, regflags);
	if (ret)
		goto fail;

	return;
fail:
	early_panic("initialization failed, %s", symerror(ret));
}

static int get_session_root(int *regflags_r)
{
	char *sessdir, *session;
	struct passwd *pw;
	int ret;

	pw = getpwuid(geteuid());
	if (pw == NULL)
		return -errno;

	if (__copperplate_setup_data.session_label == NULL) {
		ret = asprintf(&session, "anon@%d", __node_id);
		if (ret < 0)
			return -ENOMEM;
		__copperplate_setup_data.session_label = session;
		*regflags_r |= REGISTRY_ANON;
	} else if (strchr(__copperplate_setup_data.session_label, '/')) {
		warning("session name may not contain slashes");
		return -EINVAL;
	}

	ret = asprintf(&sessdir, "%s/%s/%s",
		       __copperplate_setup_data.registry_root,
		       pw->pw_name, __copperplate_setup_data.session_label);
	if (ret < 0)
		return -ENOMEM;

	__copperplate_setup_data.session_root = sessdir;

	if (__copperplate_setup_data.shared_registry)
		*regflags_r |= REGISTRY_SHARED;

	return 0;
}

static int get_session_label(const char *optarg)
{
	char *session, *grpname, *p;
	struct group *grp;
	gid_t gid;
	int ret;

	session = strdup(optarg);
	if (!session)
		return -ENOMEM;
	grpname = strrchr(session, '/');
	if (grpname == NULL)
		goto no_group;

	*grpname++ = '\0';

	if (isdigit(*grpname)) {
		gid = (gid_t)strtol(grpname, &p, 10);
		if (*p) {
			free(session);
			return -EINVAL;
		}
		errno = 0;
		grp = getgrgid(gid);
	} else {
		errno = 0;
		grp = getgrnam(grpname);
	}

	if (grp == NULL) {
		ret = errno ? -errno : -EINVAL;
		warning("invalid group %s", grpname);
		free(session);
		return ret;
	}

	__copperplate_setup_data.session_gid = grp->gr_gid;
no_group:
	__copperplate_setup_data.session_label = session;
	
	return 0;
}

define_config_tunable(session_label, const char *, label)
{
	get_session_label(label);
}

static int copperplate_init(void)
{
	int ret, regflags = 0;

	threadobj_init_key();

	ret = heapobj_pkg_init_private();
	if (ret) {
		warning("failed to initialize main private heap");
		return ret;
	}

	/*
	 * We need the session label to be known before we create the
	 * shared heap, which is named after the former.
	 */
	ret = get_session_root(&regflags);
	if (ret)
		return ret;

	ret = heapobj_pkg_init_shared();
	if (ret) {
		warning("failed to initialize main shared heap");
		return ret;
	}

	if (__copperplate_setup_data.no_registry == 0) {
		ret = registry_pkg_init(__base_setup_data.arg0, regflags);
		if (ret)
			return ret;
	}

	ret = threadobj_pkg_init((regflags & REGISTRY_ANON) != 0);
	if (ret) {
		warning("failed to initialize multi-threading package");
		return ret;
	}

	ret = timerobj_pkg_init();
	if (ret) {
		warning("failed to initialize timer support");
		return ret;
	}

	return 0;
}

static int copperplate_parse_option(int optnum, const char *optarg)
{
	size_t memsz;
	int ret;

	switch (optnum) {
	case mempool_opt:
		memsz = get_mem_size(optarg);
		if (memsz == 0)
			return -EINVAL;
		/*
		 * Emulate former sloppy syntax: values below 64k are
		 * likely to represent kilobytes, not bytes.
		 */
		if (isdigit(optarg[strlen(optarg)-1]) &&
		    memsz < 64 * 1024) {
			memsz *= 1024;
			if (__base_setup_data.no_sanity == 0)
				warning("--mem-pool-size=<size[K|M|G]>, using %Zu bytes", memsz);
		}
		__copperplate_setup_data.mem_pool = memsz;
		break;
	case session_opt:
		ret = get_session_label(optarg);
		if (ret)
			return ret;
		break;
	case regroot_opt:
		__copperplate_setup_data.registry_root = strdup(optarg);
		break;
	case shared_registry_opt:
	case no_registry_opt:
		break;
	default:
		/* Paranoid, can't happen. */
		return -EINVAL;
	}

	return 0;
}

static void copperplate_help(void)
{
	fprintf(stderr, "--mem-pool-size=<size[K|M|G]> 	size of the main heap\n");
        fprintf(stderr, "--no-registry			suppress object registration\n");
        fprintf(stderr, "--shared-registry		enable public access to registry\n");
        fprintf(stderr, "--registry-root=<path>		root path of registry\n");
        fprintf(stderr, "--session=<label>[/<group>]	enable shared session\n");
}

static struct setup_descriptor copperplate_interface = {
	.name = "copperplate",
	.init = copperplate_init,
	.options = copperplate_options,
	.parse_option = copperplate_parse_option,
	.help = copperplate_help,
};

copperplate_setup_call(copperplate_interface);

/**
 * @{
 *
 * @page api-tags API service tags
 *
 * All services from the Cobalt/POSIX library, or which belong to APIs
 * based on the Copperplate library may be restricted to particular
 * calling contexts, or entail specific side-effects.
 *
 * In dual kernel mode, the Cobalt API underlies all other
 * application-oriented APIs, providing POSIX real-time services over
 * the Cobalt real-time core. Therefore, the information below applies
 * to all application-oriented APIs available with Xenomai, such as
 * the Cobalt/POSIX library, the Alchemy API, and to all RTOS
 * emulators as well. To describe this information, each service
 * documented by this section bears a set of tags when applicable.
 *
 * The table below matches the tags used throughout the documentation
 * with the description of their meaning for the caller.
 *
 * @attention By Xenomai thread, we mean any thread created by a
 * Xenomai API service, including real-time Cobalt/POSIX threads in
 * dual kernel mode. By regular/plain POSIX thread, we mean any thread
 * directly created by the standard @a glibc-based POSIX service over
 * Mercury or Cobalt (i.e. NPTL/linuxthreads __STD(pthread_create())),
 * excluding such threads which have been promoted to the real-time
 * domain afterwards (aka "shadowed") over Cobalt.
 *
 * @par
 * <b>Context tags</b>
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Context on entry</TH></TR>
 * <TR><TD>xthread-only</TD>	<TD>Must be called from a Xenomai thread</TD></TR>
 * <TR><TD>xhandler-only</TD>	<TD>Must be called from a Xenomai handler. See note.</TD></TR>
 * <TR><TD>xcontext</TD>	<TD>May be called from any Xenomai context (thread or handler).</TD></TR>
 * <TR><TD>pthread-only</TD>	<TD>Must be called from a regular POSIX thread</TD></TR>
 * <TR><TD>thread-unrestricted</TD>	<TD>May be called from a Xenomai or regular POSIX thread indifferently</TD></TR>
 * <TR><TD>xthread-nowait</TD>	<TD>May be called from a Xenomai thread unrestricted, or from a regular thread as a non-blocking service only. See note.</TD></TR>
 * <TR><TD>unrestricted</TD>	<TD>May be called from any context previously described</TD></TR>
 * </TABLE>
 *
 * @note A Xenomai handler is used for callback-based notifications
 * from Copperplate-based APIs, such as timeouts. This context is @a
 * NOT mapped to a regular Linux signal handler, it is actually
 * underlaid by a special thread context, so that async-unsafe POSIX
 * services may be invoked internally by the API implementation when
 * running on behalf of such handler. Therefore, calling Xenomai API
 * services from asynchronous regular signal handlers is fundamentally
 * unsafe.
 *
 * @note Over Cobalt, the main thread is a particular case, which
 * starts as a regular POSIX thread, then is automatically switched to
 * a Cobalt thread as part of the initialization process, before the
 * main() routine is invoked, unless automatic bootstrap was disabled
 * (see
 * http://xenomai.org/2015/05/application-setup-and-init/#Application_entry_CC).
 *
 * @par
 * <b>Possible side-effects when running the application over the
 * Cobalt core (i.e. dual kernel configuration)</b>
 *
 * <TABLE>
 * <TR><TH>Tag</TH> <TH>Description</TH></TR>
 * <TR><TD>switch-primary</TD>		<TD>the caller may switch to primary mode</TD></TR>
 * <TR><TD>switch-secondary</TD>	<TD>the caller may switch to secondary mode</TD></TR>
 * </TABLE>
 *
 * @note As a rule of thumb, any service which might block the caller,
 * causes a switch to primary mode if invoked from secondary
 * mode. This rule might not apply in case the service can complete
 * fully from user-space without any syscall entailed, due to a
 * particular optimization (e.g. fast acquisition of semaphore
 * resources directly from user-space in the non-contended
 * case). Therefore, the switch-{primary, secondary} tags denote
 * either services which _will_ always switch the caller to the mode
 * mentioned, or _might_ have to do so, depending on the context. The
 * absence of such tag indicates that such services can complete in
 * either modes and as such will entail no switch.
 *
 * @}
 */
