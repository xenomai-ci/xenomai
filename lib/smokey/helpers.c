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
#include <stdarg.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <boilerplate/ancillaries.h>
#include <smokey/smokey.h>

int smokey_int(const char *s, struct smokey_arg *arg)
{
	char *name, *p;
	int ret;

	ret = sscanf(s, "%m[_a-z]=%m[^\n]", &name, &p);
	if (ret != 2 || !(isdigit(*p) || *p == '-'))
		return 0;

	ret = !strcmp(name, arg->name);
	if (ret)
		arg->u.n_val = atoi(p);

	free(p);
	free(name);

	return ret;
}

int smokey_bool(const char *s, struct smokey_arg *arg)
{
	int ret;

	ret = smokey_int(s, arg);
	if (ret) {
		arg->u.n_val = !!arg->u.n_val;
		return 1;
	}

	if (strcmp(s, arg->name) == 0) {
		arg->u.n_val = 1;
		return 1;
	}

	return 0;
}

int smokey_string(const char *s, struct smokey_arg *arg)
{
	char *name, *p;
	int ret;

	ret = sscanf(s, "%m[_a-z]=%m[^\n]", &name, &p);
	if (ret != 2)
		return 0;

	ret = !strcmp(name, arg->name);
	if (ret)
		arg->u.s_val = p;
	else
		free(p);

	free(name);

	return ret;
}

int smokey_size(const char *s, struct smokey_arg *arg)
{
	char *name, *p;
	int ret;

	ret = sscanf(s, "%m[_a-z]=%m[^\n]", &name, &p);
	if (ret != 2)
		return 0;

	ret = !strcmp(name, arg->name);
	if (ret) {
		arg->u.l_val = get_mem_size(p);
		if (arg->u.l_val == 0)
			ret = 0;
	}

	free(p);
	free(name);

	return ret;
}

int smokey_parse_args(struct smokey_test *t,
		      int argc, char *const argv[])
{
	int matched = 0, n, ac;
	struct smokey_arg *arg;

	for (arg = t->args, ac = 0;
	     arg->name && ac < t->nargs; arg++, ac++) {
		for (n = 1; n < argc; n++) {
			arg->matched = !!arg->parser(argv[n], arg);
			if (arg->matched) {
				matched++;
				break;
			}
		}
	}

	return matched;
}

struct smokey_arg *smokey_lookup_arg(struct smokey_test *t,
				     const char *name)
{
	struct smokey_arg *arg = NULL;
	int ac;

	for (arg = t->args, ac = 0;
	     arg->name && ac < t->nargs; arg++, ac++) {
		if (strcmp(arg->name, name) == 0)
			return arg;
	}

	/* Assume this is fatal. */
	panic("test %s has no argument \"%s\"",
	      t->name, name);
}

void smokey_note(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (smokey_verbose_mode) {
		__RT(vfprintf(stdout, fmt, ap));
		__RT(fprintf(stdout, "\n"));
	}

	va_end(ap);
}

void smokey_vatrace(const char *fmt, va_list ap)
{
	if (smokey_verbose_mode > 1) {
		__RT(vfprintf(stdout, fmt, ap));
		__RT(fprintf(stdout, "\n"));
	}
}

void smokey_trace(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	smokey_vatrace(fmt, ap);
	va_end(ap);
}

void __smokey_warning(const char *file, int lineno,
		      const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (smokey_verbose_mode) {
		__RT(fprintf(stderr, "%s:%d, ", basename(file), lineno));
		__RT(vfprintf(stderr, fmt, ap));
		__RT(fprintf(stderr, "\n"));
	}

	va_end(ap);
}

int smokey_barrier_init(struct smokey_barrier *b)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	int ret;

	b->signaled = 0;
	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_NORMAL);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_NONE);
	ret = __RT(pthread_mutex_init(&b->lock, &mattr));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_PRIVATE);
	ret = __RT(pthread_cond_init(&b->barrier, &cattr));
	pthread_condattr_destroy(&cattr);
	if (ret)
		__RT(pthread_mutex_destroy(&b->lock));

	return ret;
}

void smokey_barrier_destroy(struct smokey_barrier *b)
{
	__RT(pthread_cond_destroy(&b->barrier));
	__RT(pthread_mutex_destroy(&b->lock));
}

int smokey_barrier_wait(struct smokey_barrier *b)
{
	int ret = 0;
	
	__RT(pthread_mutex_lock(&b->lock));

	while (!b->signaled) {
		ret = __RT(pthread_cond_wait(&b->barrier, &b->lock));
		if (ret)
			break;
	}

	__RT(pthread_mutex_unlock(&b->lock));

	return ret;
}

int smokey_barrier_timedwait(struct smokey_barrier *b, struct timespec *ts)
{
	int ret = 0;
	
	__RT(pthread_mutex_lock(&b->lock));

	while (!b->signaled) {
		ret = __RT(pthread_cond_timedwait(&b->barrier,
						  &b->lock, ts));
		if (ret)
			break;
	}

	__RT(pthread_mutex_unlock(&b->lock));

	return ret;
}

void smokey_barrier_release(struct smokey_barrier *b)
{
	__RT(pthread_mutex_lock(&b->lock));
	b->signaled = 1;
	__RT(pthread_cond_broadcast(&b->barrier));
	__RT(pthread_mutex_unlock(&b->lock));
}

int smokey_fork_exec(const char *path, const char *arg)
{
	int wstatus = 0;
	pid_t pid;

	pid = do_fork();
	switch (pid) {
	case -1:
		error(1, errno, "fork/vfork");
	case 0:
		execlp(path, arg, NULL);
		error(1, errno, "execl %s", path);
	default:
		waitpid(pid, &wstatus, 0);
	}

	if WIFEXITED(wstatus)
		return WEXITSTATUS(wstatus);

	if WIFSIGNALED(wstatus)
		fprintf(stderr, "%s %s\n",
			strsignal(WTERMSIG(wstatus)),
			WCOREDUMP(wstatus) ? "(core dumped)" : "");
	return 1;

}

static bool module_loaded(const char *name)
{
	bool result = false;
	char buffer[128];
	int len;
	FILE *fp;

	fp = fopen("/proc/modules", "r");
	if (fp == NULL)
		return false;

	len = strlen(name);

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (strncmp(buffer, name, len) == 0 &&
		    len < sizeof(buffer) && buffer[len] == ' ') {
			result = true;
			break;
		}
	}

	fclose(fp);

	return result;
}

#define SMOKEY_MOD_NUM       32
static char *smokey_modules[SMOKEY_MOD_NUM];

int smokey_modprobe(const char *name, bool silent)
{
	char buffer[128];
	int err, i, midx = -1;

	if (!name)
		return -EINVAL;

	if (module_loaded(name)) {
		smokey_trace("%s module already loaded", name);
		return 0;
	}

	for (i = 0; i < SMOKEY_MOD_NUM; i++) {
		if (!smokey_modules[i]) {
			midx = i;
			break;
		}
	}

	if (midx < 0)
		return -EFAULT;

	smokey_trace("%s module not there: modprobing", name);

	err = smokey_check_errno(
		snprintf(buffer, sizeof(buffer), "modprobe %s %s", name,
			 silent ? "2>/dev/null" : ""));
	if (err < 0)
		return err;

	err = smokey_check_errno(system(buffer));
	if (err < 0)
		return err;

	if (!WIFEXITED(err) || WEXITSTATUS(err) != 0) {
		if (!silent)
			smokey_warning("%s: abnormal exit", buffer);
		return -EINVAL;
	}

	if (!err && module_loaded(name))
		smokey_modules[midx] = strdup(name);

	return err;
}

int smokey_rmmod(const char *name)
{
	char buffer[128];
	int i, midx = -1, err;

	if (!name)
		return -EINVAL;

	for (i = 0; i < SMOKEY_MOD_NUM; i++) {
		if (smokey_modules[i] && !strcmp(smokey_modules[i], name)) {
			midx = i;
			break;
		}
	}

	if (midx < 0) {
		smokey_trace("%s module was there on entry, keeping it", name);
		return 0;
	}

	smokey_trace("unloading %s module", name);

	err = smokey_check_errno(
		snprintf(buffer, sizeof(buffer), "rmmod %s", name));
	if (err < 0)
		return err;

	err = smokey_check_errno(system(buffer));
	if (err < 0)
		return err;

	if (!WIFEXITED(err) || WEXITSTATUS(err) != 0) {
		smokey_warning("%s: abnormal exit", buffer);
		return -EINVAL;
	}

	free(smokey_modules[midx]);
	smokey_modules[midx] = NULL;

	return err;
}
