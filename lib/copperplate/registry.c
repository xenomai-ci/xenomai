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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <memory.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <semaphore.h>
#include <fuse.h>
#include <xeno_config.h>
#include "boilerplate/atomic.h"
#include "boilerplate/hash.h"
#include "copperplate/heapobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/registry.h"
#include "copperplate/registry-obstack.h"
#include "copperplate/clockobj.h"
#include "boilerplate/lock.h"
#include "copperplate/debug.h"
#include "xenomai/init.h"
#include "internal.h"

/*
 * CAUTION: this code shall NOT refer to the shared heap in any way,
 * only private storage is allowed here: sysregd won't map the main
 * shared heap permanently, but only in a transitory manner via
 * heapobj_bind_session() when reading a /system node.
 */

static pthread_t regfs_thid;

struct regfs_data {
	const char *arg0;
	char *mountpt;
	int flags;
	sem_t sync;
	int status;
	pthread_mutex_t lock;
	struct pvhash_table files;
	struct pvhash_table dirs;
};

static inline struct regfs_data *regfs_get_context(void)
{
	static struct regfs_data data;

	return &data;
}

struct regfs_dir {
	char *path;
	const char *basename;
	struct pvhashobj hobj;
	struct pvlistobj file_list;
	struct pvlistobj dir_list;
	int ndirs, nfiles;
	struct timespec ctime;
	struct pvholder link;
};

const static struct pvhash_operations pvhash_operations = {
	.compare = memcmp,
};

int registry_add_dir(const char *fmt, ...)
{
	struct regfs_data *p = regfs_get_context();
	char path[PATH_MAX], *basename;
	struct regfs_dir *parent, *d;
	struct pvhashobj *hobj;
	struct timespec now;
	int ret, state;
	va_list ap;

	if (__copperplate_setup_data.no_registry)
		return 0;

	va_start(ap, fmt);
	vsnprintf(path, PATH_MAX, fmt, ap);
	va_end(ap);

	basename = strrchr(path, '/');
	if (basename == NULL)
		return __bt(-EINVAL);

	__RT(clock_gettime(CLOCK_COPPERPLATE, &now));

	write_lock_safe(&p->lock, state);

	d = pvmalloc(sizeof(*d));
	if (d == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	pvholder_init(&d->link);
	d->path = pvstrdup(path);

	if (strcmp(path, "/")) {
		d->basename = d->path + (basename - path) + 1;
		if (path == basename)
			basename++;
		*basename = '\0';
		hobj = pvhash_search(&p->dirs, path, strlen(path),
				     &pvhash_operations);
		if (hobj == NULL) {
			ret = -ENOENT;
			goto fail;
		}
		parent = container_of(hobj, struct regfs_dir, hobj);
		pvlist_append(&d->link, &parent->dir_list);
		parent->ndirs++;
	} else
		d->basename = d->path;

	pvlist_init(&d->file_list);
	pvlist_init(&d->dir_list);
	d->ndirs = d->nfiles = 0;
	d->ctime = now;
	ret = pvhash_enter(&p->dirs, d->path, strlen(d->path), &d->hobj,
			   &pvhash_operations);
	if (ret) {
	fail:
		pvfree(d->path);
		pvfree(d);
	}
done:
	write_unlock_safe(&p->lock, state);

	return __bt(ret);
}

int registry_init_file(struct fsobj *fsobj,
		       const struct registry_operations *ops,
		       size_t privsz)
{
	pthread_mutexattr_t mattr;
	int ret;

	if (__copperplate_setup_data.no_registry)
		return 0;

	fsobj->path = NULL;
	fsobj->ops = ops;
	fsobj->privsz = privsz;
	pvholder_init(&fsobj->link);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&fsobj->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);

	return ret;
}

int registry_add_file(struct fsobj *fsobj, int mode, const char *fmt, ...)
{
	struct regfs_data *p = regfs_get_context();
	char path[PATH_MAX], *basename, *dir;
	struct pvhashobj *hobj;
	struct regfs_dir *d;
	int ret, state;
	va_list ap;

	if (__copperplate_setup_data.no_registry)
		return 0;

	va_start(ap, fmt);
	vsnprintf(path, PATH_MAX, fmt, ap);
	va_end(ap);

	basename = strrchr(path, '/');
	if (basename == NULL)
		return __bt(-EINVAL);

	fsobj->path = pvstrdup(path);
	fsobj->basename = fsobj->path + (basename - path) + 1;
	fsobj->mode = mode & O_ACCMODE;
	__RT(clock_gettime(CLOCK_COPPERPLATE, &fsobj->ctime));
	fsobj->mtime = fsobj->ctime;

	write_lock_safe(&p->lock, state);

	ret = pvhash_enter(&p->files, fsobj->path, strlen(fsobj->path),
			   &fsobj->hobj, &pvhash_operations);
	if (ret)
		goto fail;

	*basename = '\0';
	dir = basename == path ? "/" : path;
	hobj = pvhash_search(&p->dirs, dir, strlen(dir),
			     &pvhash_operations);
	if (hobj == NULL) {
		ret = -ENOENT;
	fail:
		pvhash_remove(&p->files, &fsobj->hobj, &pvhash_operations);
		pvfree(fsobj->path);
		fsobj->path = NULL;
		goto done;
	}

	d = container_of(hobj, struct regfs_dir, hobj);
	pvlist_append(&fsobj->link, &d->file_list);
	d->nfiles++;
	fsobj->dir = d;
done:
	write_unlock_safe(&p->lock, state);

	return __bt(ret);
}

void registry_destroy_file(struct fsobj *fsobj)
{
	struct regfs_data *p = regfs_get_context();
	struct regfs_dir *d;
	int state;

	if (__copperplate_setup_data.no_registry)
		return;

	write_lock_safe(&p->lock, state);

	if (fsobj->path == NULL)
		goto out;	/* Not registered. */

	pvhash_remove(&p->files, &fsobj->hobj, &pvhash_operations);
	/*
	 * We are covered by a previous call to write_lock_safe(), so
	 * we may nest pthread_mutex_lock() directly.
	 */
	__RT(pthread_mutex_lock(&fsobj->lock));
	d = fsobj->dir;
	pvlist_remove(&fsobj->link);
	d->nfiles--;
	assert(d->nfiles >= 0);
	pvfree(fsobj->path);
	__RT(pthread_mutex_unlock(&fsobj->lock));
out:
	__RT(pthread_mutex_destroy(&fsobj->lock));
	write_unlock_safe(&p->lock, state);
}

void registry_touch_file(struct fsobj *fsobj)
{
	if (__copperplate_setup_data.no_registry)
		return;

	__RT(clock_gettime(CLOCK_COPPERPLATE, &fsobj->mtime));
}

static int regfs_getattr(const char *path, struct stat *sbuf)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct regfs_dir *d;
	struct fsobj *fsobj;
	int ret = 0;

	memset(sbuf, 0, sizeof(*sbuf));

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->dirs, path, strlen(path),
			     &pvhash_operations);
	if (hobj) {
		d = container_of(hobj, struct regfs_dir, hobj);
		sbuf->st_mode = S_IFDIR | 0755;
		sbuf->st_nlink = d->ndirs + 2;
		sbuf->st_atim = d->ctime;
		sbuf->st_ctim = d->ctime;
		sbuf->st_mtim = d->ctime;
		goto done;
	}

	hobj = pvhash_search(&p->files, path, strlen(path),
			     &pvhash_operations);
	if (hobj) {
		fsobj = container_of(hobj, struct fsobj, hobj);
		sbuf->st_mode = S_IFREG;
		switch (fsobj->mode) {
		case O_RDONLY:
			sbuf->st_mode |= 0444;
			break;
		case O_WRONLY:
			sbuf->st_mode |= 0222;
			break;
		case O_RDWR:
			sbuf->st_mode |= 0666;
			break;
		}
		sbuf->st_nlink = 1;
		sbuf->st_size = 32768; /* XXX: this should be dynamic. */
		sbuf->st_atim = fsobj->mtime;
		sbuf->st_ctim = fsobj->ctime;
		sbuf->st_mtim = fsobj->mtime;
	} else
		ret = -ENOENT;
done:
	read_unlock(&p->lock);

	return ret;
}

static int regfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct regfs_dir *d, *subd;
	struct pvhashobj *hobj;
	struct fsobj *fsobj;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->dirs, path, strlen(path),
			     &pvhash_operations);
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOENT);
	}

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	d = container_of(hobj, struct regfs_dir, hobj);

	if (!pvlist_empty(&d->dir_list)) {
		pvlist_for_each_entry(subd, &d->dir_list, link) {
			/* We don't output empty directories. */
			if (subd->ndirs + subd->nfiles == 0)
				continue;
			if (filler(buf, subd->basename, NULL, 0))
				break;
		}
	}

	if (!pvlist_empty(&d->file_list)) {
		pvlist_for_each_entry(fsobj, &d->file_list, link)
			if (filler(buf, fsobj->basename, NULL, 0))
				break;
	}

	read_unlock(&p->lock);

	return 0;
}

static int regfs_open(const char *path, struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	struct service svc;
	int ret = 0;
	void *priv;

	push_cleanup_lock(&p->lock);
	read_lock(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path),
			     &pvhash_operations);
	if (hobj == NULL) {
		ret = -ENOENT;
		goto done;
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (((fi->flags + 1) & (fsobj->mode + 1)) == 0) {
		ret = -EACCES;
		goto done;
	}

	if (fsobj->privsz) {
		priv = malloc(fsobj->privsz);
		if (priv == NULL) {
			ret = -ENOMEM;
			goto done;
		}
	} else
		priv = NULL;

	fi->fh = (uintptr_t)priv;
	if (fsobj->ops->open) {
		CANCEL_DEFER(svc);
		ret = __bt(fsobj->ops->open(fsobj, priv));
		CANCEL_RESTORE(svc);
	}
done:
	read_unlock(&p->lock);
	pop_cleanup_lock(&p->lock);

	return __bt(ret);
}

static int regfs_release(const char *path, struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	struct service svc;
	int ret = 0;
	void *priv;

	push_cleanup_lock(&p->lock);
	read_lock(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path),
			     &pvhash_operations);
	if (hobj == NULL) {
		ret = -ENOENT;
		goto done;
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	priv = (void *)(uintptr_t)fi->fh;
	if (fsobj->ops->release) {
		CANCEL_DEFER(svc);
		ret = __bt(fsobj->ops->release(fsobj, priv));
		CANCEL_RESTORE(svc);
	}
	if (priv)
		free(priv);
done:
	read_unlock(&p->lock);
	pop_cleanup_lock(&p->lock);

	return __bt(ret);
}

static int regfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	struct service svc;
	void *priv;
	int ret;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path),
			     &pvhash_operations);
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-EIO);
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (fsobj->ops->read == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOSYS);
	}

	push_cleanup_lock(&fsobj->lock);
	read_lock(&fsobj->lock);
	read_unlock(&p->lock);
	priv = (void *)(uintptr_t)fi->fh;
	CANCEL_DEFER(svc);
	ret = fsobj->ops->read(fsobj, buf, size, offset, priv);
	CANCEL_RESTORE(svc);
	read_unlock(&fsobj->lock);
	pop_cleanup_lock(&fsobj->lock);

	return __bt(ret);
}

static int regfs_write(const char *path, const char *buf, size_t size, off_t offset,
		       struct fuse_file_info *fi)
{
	struct regfs_data *p = regfs_get_context();
	struct pvhashobj *hobj;
	struct fsobj *fsobj;
	struct service svc;
	void *priv;
	int ret;

	read_lock_nocancel(&p->lock);

	hobj = pvhash_search(&p->files, path, strlen(path),
			     &pvhash_operations);
	if (hobj == NULL) {
		read_unlock(&p->lock);
		return __bt(-EIO);
	}

	fsobj = container_of(hobj, struct fsobj, hobj);
	if (fsobj->ops->write == NULL) {
		read_unlock(&p->lock);
		return __bt(-ENOSYS);
	}

	push_cleanup_lock(&fsobj->lock);
	read_lock(&fsobj->lock);
	read_unlock(&p->lock);
	priv = (void *)(uintptr_t)fi->fh;
	CANCEL_DEFER(svc);
	ret = fsobj->ops->write(fsobj, buf, size, offset, priv);
	CANCEL_RESTORE(svc);
	read_unlock(&fsobj->lock);
	pop_cleanup_lock(&fsobj->lock);

	return __bt(ret);
}

static int regfs_truncate(const char *path, off_t offset)
{
	return 0;
}

static int regfs_chmod(const char *path, mode_t mode)
{
	return 0;
}

static int regfs_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}

static void *regfs_init(void)
{
	struct regfs_data *p = regfs_get_context();
	struct sigaction sa;

	/*
	 * Override annoying FUSE settings. Unless the application
	 * tells otherwise, we want the emulator to exit upon common
	 * termination signals.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);

	p->status = 0;	/* all ok. */
	__STD(sem_post(&p->sync));

	return p;
}

static struct fuse_operations regfs_opts = {
	.init		= regfs_init,
	.getattr	= regfs_getattr,
	.readdir	= regfs_readdir,
	.open		= regfs_open,
	.release	= regfs_release,
	.read		= regfs_read,
	.write		= regfs_write,
	/* Those must be defined for writing to files too. */
	.truncate	= regfs_truncate,
	.chown		= regfs_chown,
	.chmod		= regfs_chmod,
};

static void *registry_thread(void *arg)
{
	struct regfs_data *p = arg;
	char *av[7];
	int ret;

	av[0] = (char *)p->arg0;
	av[1] = "-s";
	av[2] = "-f";
	av[3] = p->mountpt;
	av[4] = "-o";
	av[5] = p->flags & REGISTRY_SHARED ?
		"default_permissions,allow_other"
		: "default_permissions";
	av[6] = NULL;

	/*
	 * Once connected to sysregd, we don't have to care for the
	 * mount point, sysregd will umount(2) it when we go away.
	 */
	ret = fuse_main(6, av, &regfs_opts);
	if (ret) {
		early_warning("can't mount registry onto %s", p->mountpt);
		/* Attempt to figure out why we failed. */
		ret = access(p->mountpt, F_OK);
		p->status = ret ? -errno : -EPERM;
		__STD(sem_post(&p->sync));
		return (void *)(long)ret;
	}

	return NULL;
}

static pid_t regd_pid;

static void sigchld_handler(int sig)
{
	smp_rmb();
	if (regd_pid &&	waitpid(regd_pid, NULL, WNOHANG) == regd_pid)
		regd_pid = 0;
}

static int spawn_daemon(const char *sessdir, int flags)
{
	struct sigaction sa;
	char *path, *av[7];
	int ret, n = 0;
	pid_t pid;

	ret = asprintf(&path, "%s/sbin/sysregd", CONFIG_XENO_PREFIX);
	if (ret < 0)
		return -ENOMEM;

	/*
	 * We want to allow application code to wait for children
	 * exits explicitly and selectively using wait*() calls, while
	 * preventing a failing sysregd to move to the zombie
	 * state. Therefore, bluntly leaving the SIGCHLD disposition
	 * to SIG_IGN upon return from this routine is not an option.
	 *
	 * To solve this issue, first we ignore SIGCHLD to plug a
	 * potential race while forking the daemon, then we trap it to
	 * a valid handler afterwards, once we know the daemon
	 * pid. This handler will selectively reap the registry
	 * daemon, and only this process, leaving all options open to
	 * the application code for reaping its own children as it
	 * sees fit.
	 */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	av[0] = "sysregd";
	av[1] = "--daemon";
	av[2] = "--root";
	av[3] = (char *)sessdir;
	n = 4;
	if (flags & REGISTRY_ANON)
		av[n++] = "--anon";
	if (flags & REGISTRY_SHARED)
		av[n++] = "--shared";

	av[n] = NULL;

	pid = vfork();
	switch (pid) {
	case 0:
		execv(path, av);
		_exit(1);
	case -1:
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);
		ret = -errno;
		break;
	default:
		/*
		 * Make sure we sleep at least 200 ms regardless of
		 * signal receipts.
		 */
		while (usleep(200000) > 0) ;
		regd_pid = pid;
		compiler_barrier();
		sa.sa_handler = sigchld_handler;
		sa.sa_flags = SA_RESTART;
		sigaction(SIGCHLD, &sa, NULL);
		ret = 0;
		break;
	}

	free(path);

	return ret;
}

static int connect_regd(const char *sessdir, char **mountpt, int flags)
{
	struct sockaddr_un sun;
	int s, ret, retries;
	unsigned int hash;
	socklen_t addrlen;

	*mountpt = malloc(PATH_MAX);
	if (*mountpt == NULL)
		return -ENOMEM;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	hash = __hash_key(sessdir, strlen(sessdir), 0);
	snprintf(sun.sun_path, sizeof(sun.sun_path), "X%X-xenomai", hash);
	addrlen = offsetof(struct sockaddr_un, sun_path) + strlen(sun.sun_path);
	sun.sun_path[0] = '\0';

	for (retries = 0; retries < 3; retries++) {
		s = __STD(socket(AF_UNIX, SOCK_SEQPACKET|SOCK_CLOEXEC, 0));
		if (s < 0) {
			ret = -errno;
			free(*mountpt);
			return ret;
		}
		ret = __STD(connect(s, (struct sockaddr *)&sun, addrlen));
		if (ret == 0) {
			ret = __STD(recv(s, *mountpt, PATH_MAX, 0));
			if (ret > 0)
				return 0;
		}
		__STD(close(s));
		ret = spawn_daemon(sessdir, flags);
		if (ret)
			break;
		ret = -EAGAIN;
	}

	free(*mountpt);

	early_warning("cannot connect to registry daemon");

	return ret;
}

static void pkg_cleanup(void)
{
	registry_pkg_destroy();
}

int __registry_pkg_init(const char *arg0, char *mountpt, int flags)
{
	struct regfs_data *p = regfs_get_context();
	pthread_mutexattr_t mattr;
	struct sched_param schedp;
	pthread_attr_t thattr;
	int ret;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&p->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	pvhash_init(&p->files);
	pvhash_init(&p->dirs);

	registry_add_dir("/");	/* Create the fs root. */

	/* We want a SCHED_OTHER thread. */
	pthread_attr_init(&thattr);
	pthread_attr_setinheritsched(&thattr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&thattr, SCHED_OTHER);
	schedp.sched_priority = 0;
	pthread_attr_setschedparam(&thattr, &schedp);
	/*
	 * Memory is locked as the process data grows, so we set a
	 * smaller stack size for the fs thread than the default 8mb
	 * set by the Glibc.
	 */
	pthread_attr_setstacksize(&thattr, PTHREAD_STACK_DEFAULT);
	pthread_attr_setscope(&thattr, PTHREAD_SCOPE_PROCESS);
	p->arg0 = arg0;
	p->mountpt = mountpt;
	p->flags = flags;
	p->status = -EINVAL;
	__STD(sem_init(&p->sync, 0, 0));

	/*
	 * Start the FUSE filesystem daemon. Over Cobalt, it runs as a
	 * non real-time Xenomai shadow, so that it may synchronize on
	 * real-time objects.
	 */
	ret = __bt(-__RT(pthread_create(&regfs_thid, &thattr,
					registry_thread, p)));
	if (ret)
		return ret;

	/*
	 * We synchronize with regfs_init() to wait for FUSE to
	 * complete all its init chores before returning to our
	 * caller.
	 */
	for (;;) {
		ret = __STD(sem_wait(&p->sync));
		if (ret == 0)
			break;
		if (errno != EINTR)
			return __bt(-errno);
	}

	atexit(pkg_cleanup);

	return p->status;
}

int registry_pkg_init(const char *arg0, int flags)
{
	char *mountpt;
	int ret;

	ret = connect_regd(__copperplate_setup_data.session_root,
			   &mountpt, flags);
	if (ret)
		return __bt(ret);

	return __bt(__registry_pkg_init(arg0, mountpt, flags));
}

void registry_pkg_destroy(void)
{
	if (regfs_thid) {
		pthread_cancel(regfs_thid);
		pthread_join(regfs_thid, NULL);
		regfs_thid = 0;
	}
}

int fsobj_obstack_release(struct fsobj *fsobj, void *priv)
{
	fsobstack_destroy(priv);

	return 0;
}

ssize_t fsobj_obstack_read(struct fsobj *fsobj,
			   char *buf, size_t size, off_t offset,
			   void *priv)
{
	return fsobstack_pull(priv, buf, size);
}

int fsobstack_grow_format(struct fsobstack *o, const char *fmt, ...)
{
	char buf[256], *p = buf;
	int len = sizeof(buf), n;
	va_list ap;

	for (;;) {
	       va_start(ap, fmt);
	       n = vsnprintf(p, len, fmt, ap);
	       va_end(ap);

	       if (n > 0 && n < len)
		       obstack_grow(&o->obstack, p, n);

	       if (p != buf)
		       free(p);

	       if (n < len)
		       return n < 0 ? -EINVAL : n;

	       len = n + 1;
	       p = malloc(len);
	       if (p == NULL)
		       break;
	   }

	return -ENOMEM;
}

void fsobstack_grow_string(struct fsobstack *o, const char *s)
{
	obstack_grow(&o->obstack, s, strlen(s));
}

void fsobstack_grow_char(struct fsobstack *o, char c)
{
	obstack_1grow(&o->obstack, c);
}

int fsobstack_grow_file(struct fsobstack *o, const char *path)
{
	int len = 0;
	FILE *fp;
	int c;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -errno;

	for (;;) {
		c = fgetc(fp);
		if (c == EOF) {
			if (ferror(fp))
				len = -errno;
			break;
		}
		obstack_1grow(&o->obstack, c);
		len++;
	}

	fclose(fp);

	return len;
}

ssize_t fsobstack_pull(struct fsobstack *o, char *buf, size_t size)
{
	size_t len;

	if (o->data == NULL)	/* Not finished. */
		return -EIO;

	len = o->len;
	if (len > 0) {
		if (len > size)
			len = size;
		o->len -= len;
		memcpy(buf, o->data, len);
		o->data += len;
	}

	return len;
}

static int collect_wait_list(struct fsobstack *o,
			     struct syncobj *sobj,
			     struct listobj *wait_list,
			     int *wait_count,
			     struct fsobstack_syncops *ops)
{
	struct threadobj *thobj;
	struct syncstate syns;
	struct obstack cache;
	struct service svc;
	int count, ret;
	void *p, *e;

	obstack_init(&cache);
	CANCEL_DEFER(svc);
redo:
	smp_rmb();
	count = *wait_count;
	if (count == 0)
		goto out;

	/* Pre-allocate the obstack room without holding any lock. */
	ret = ops->prepare_cache(o, &cache, count);
	if (ret)
		goto out;

	ret = syncobj_lock(sobj, &syns);
	if (ret) {
		count = ret;
		goto out;
	}

	/* Re-validate the previous item count under lock. */
	if (count != *wait_count) {
		syncobj_unlock(sobj, &syns);
		obstack_free(&cache, NULL);
		goto redo;
	}

	p = obstack_base(&cache);
	list_for_each_entry(thobj, wait_list, wait_link)
		p += ops->collect_data(p, thobj);

	syncobj_unlock(sobj, &syns);

	/*
	 * Some may want to format data directly from the collect
	 * handler, when no gain is expected from splitting the
	 * collect and format steps. In that case, we may have no
	 * format handler.
	 */
	e = obstack_next_free(&cache);
	p = obstack_finish(&cache);
	if (ops->format_data == NULL) {
		if (e != p)
			obstack_grow(&o->obstack, p, e - p);
		goto out;
	}

	/* Finally, format the output without holding any lock. */
	do
		p += ops->format_data(o, p);
	while (p < e);
out:
	CANCEL_RESTORE(svc);
	obstack_free(&cache, NULL);

	return count;
}

int fsobstack_grow_syncobj_grant(struct fsobstack *o, struct syncobj *sobj,
				 struct fsobstack_syncops *ops)
{
	return collect_wait_list(o, sobj, &sobj->grant_list,
				 &sobj->grant_count, ops);
}

int fsobstack_grow_syncobj_drain(struct fsobstack *o, struct syncobj *sobj,
				 struct fsobstack_syncops *ops)
{
	return collect_wait_list(o, sobj, &sobj->drain_list,
				 &sobj->drain_count, ops);
}
