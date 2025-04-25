/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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

 /*
 * This file implements object clusters, to group various related
 * runtime objects in named tables. Objects within clusters are
 * indexed on a string label. Depending on whether shared
 * multi-processing mode is enabled, clusters may be persistent in the
 * main heap.
 *
 * In its simplest form - when shared multi-processing is disabled -,
 * a cluster is basically a private hash table only known from the
 * process who created it.
 *
 * When shared multi-processing mode is enabled, a cluster is a shared
 * hash table indexed on a unique name within the main catalog.
 * Therefore, all objects referred to by the cluster should be laid
 * into the main heap as well.  Multiple processes attached to the
 * same copperplate session do share the same main heap. Therefore,
 * they may share objects by providing:
 *
 * - the name of the cluster.
 * - the name of the object to retrieve from the cluster.
 *
 * Having objects shared between processes introduces the requirement
 * to deal with stale objects, created by processes that don't exist
 * anymore when a lookup is performed on a cluster by another
 * process. We deal with this issue as simply as we can, as follows:
 *
 * - each object referenced to by a cluster bears a "creator node"
 * identifier. This is basically the system-wide linux TID of the
 * process owning the thread which has initially added the object to
 * the cluster (i.e. getpid() as returned from the NPTL).
 *
 * - upon a lookup operation in the cluster which matches an object in
 * the table, the process who introduced the object is probed for
 * existence. If the process is gone, we silently drop the reference
 * to the orphaned object from the cluster, and return a failed lookup
 * status. Otherwise, the lookup succeeds.
 *
 * - when an attempt is made to index an object into cluster, any
 * conflicting object which bears the same name is checked for
 * staleness as described for the lookup operation. However, the
 * insertion succeeds after the reference to a conflicting stale
 * object was silently discarded.
 *
 * The test for existence based on the linux TID may return spurious
 * "true" results in case an object was created by a long gone
 * process, whose TID was eventually reused for a newer process,
 * before the process who initialized the main heap has exited. In
 * theory, this situation may happen; in practice, 1) the TID
 * generator has to wrap around fully before this happens, 2) multiple
 * processes sharing objects via a cluster are normally co-operating
 * to implement a global functionality. In the event of a process
 * exit, it is likely that the whole application system should be
 * reinited, thus the main (session) heap would be reset, which would
 * in turn clear the issue.
 *
 * In the worst case, using a stale object would never cause bad
 * memory references, since a clustered object - and all the memory
 * references it does via its members - must be laid into the main
 * heap, which is persistent until the last process attached to it
 * leaves the session.
 *
 * This stale object detection is essentially a sanity mechanism to
 * cleanup obviously wrong references from clusters after some process
 * died unexpectedly. Under normal circumstances, for an orderly exit,
 * a process should remove all references to objects it has created
 * from existing clusters, before eventually freeing those objects.
 *
 * In addition to the basic cluster object, the synchronizing cluster
 * (struct syncluster) provides support for waiting for a given object
 * to appear in the dictionary.
 */

#include <errno.h>
#include <string.h>
#include "copperplate/heapobj.h"
#include "copperplate/cluster.h"
#include "copperplate/threadobj.h"
#include "internal.h"

const static struct hash_operations hash_operations;

struct cluster_walk_data {
	struct cluster *c;
	int (*walk)(struct cluster *c,
		    struct clusterobj *cobj);
};

struct pvcluster_walk_data {
	struct pvcluster *c;
	int (*walk)(struct pvcluster *c,
		    struct pvclusterobj *cobj);
};

#ifdef CONFIG_XENO_PSHARED

int cluster_init(struct cluster *c, const char *name)
{
	struct dictionary *d;
	struct hashobj *hobj;
	int ret;

	/*
	 * NOTE: it does not make sense to destroy a shared cluster
	 * since other processes from the same session will likely
	 * have references on it, so there is no cluster_destroy()
	 * routine on purpose. When all processes from the session are
	 * gone, the shared heap is cleared next time the application
	 * boots, so there is really no use of deleting shared
	 * clusters.
	 */
redo:
	hobj = hash_search(&main_catalog, name, strlen(name),
			   &hash_operations);
	if (hobj) {
		d = container_of(hobj, struct dictionary, hobj);
		ret = 0;
		goto out;
	}

	d = xnmalloc(sizeof(*d));
	if (d == NULL)
		return __bt(-ENOMEM);

	hash_init(&d->table);
	ret = hash_enter(&main_catalog, name, strlen(name), &d->hobj,
			 &hash_operations);
	/*
	 * If someone managed to slip in, creating the cluster between
	 * the table look up and indexing the new cluster, retry the
	 * whole process.
	 */
	if (ret == -EEXIST) {
		hash_destroy(&d->table);
		xnfree(d);
		goto redo;
	}
out:
	c->d = d;

	return __bt(ret);
}

static int cluster_probe(struct hashobj *hobj)
{
	struct clusterobj *cobj;

	cobj = container_of(hobj, struct clusterobj, hobj);
	if (cobj->cnode == __node_id)
		return 1; /* Trivial check: is it ours? */

	/*
	 * The node identifier is actually the main thread pid, so if
	 * we can send the latter a signal, the node is deemed active.
	 * Over Cobalt, the main thread is always shadowed, therefore
	 * we may use Cobalt's kill() service to probe for it.
	 * Receiving EPERM does mean that we found an active node,
	 * just that we don't have the credentials to actually send it
	 * a signal.
	 */
	return copperplate_probe_tid(cobj->cnode) == 0;
}

int cluster_addobj(struct cluster *c, const char *name,
		   struct clusterobj *cobj)
{
	cobj->cnode = __node_id;
	/*
	 * Add object to cluster and probe conflicting entries for
	 * owner node existence, overwriting dead instances on the
	 * fly.
	 */
	return hash_enter_probe(&c->d->table, name, strlen(name),
				&cobj->hobj, &hash_operations);
}

int cluster_addobj_dup(struct cluster *c, const char *name,
		       struct clusterobj *cobj)
{
	cobj->cnode = __node_id;
	/*
	 * Same as cluster_addobj(), but allows for duplicate keys in
	 * live objects.
	 */
	return hash_enter_probe_dup(&c->d->table, name, strlen(name),
				    &cobj->hobj, &hash_operations);
}

int cluster_delobj(struct cluster *c, struct clusterobj *cobj)
{
	return __bt(hash_remove(&c->d->table, &cobj->hobj, &hash_operations));
}

struct clusterobj *cluster_findobj(struct cluster *c, const char *name)
{
	struct hashobj *hobj;

	/*
	 * Search for object entry and probe for owner node existence,
	 * discarding dead instances on the fly.
	 */
	hobj = hash_search_probe(&c->d->table, name, strlen(name),
				 &hash_operations);
	if (hobj == NULL)
		return NULL;

	return container_of(hobj, struct clusterobj, hobj);
}

static int __cluster_walk(struct hash_table *t, struct hashobj *hobj, void *arg)
{
	struct cluster_walk_data *wd = arg;
	struct clusterobj *cobj;

	cobj = container_of(hobj, struct clusterobj, hobj);

	return wd->walk(wd->c, cobj);
}
  
int cluster_walk(struct cluster *c,
		 int (*walk)(struct cluster *c,
			     struct clusterobj *cobj))
{
	struct cluster_walk_data wd = {
		.c = c,
		.walk = walk,
	};
	return hash_walk(&c->d->table, __cluster_walk, &wd);
}

int syncluster_init(struct syncluster *sc, const char *name)
{
	struct syndictionary *d;
	struct hashobj *hobj;
	int ret;

redo:
	hobj = hash_search(&main_catalog, name, strlen(name),
			   &hash_operations);
	if (hobj) {
		sc->d = container_of(hobj, struct syndictionary, hobj);
		return 0;
	}

	d = xnmalloc(sizeof(*d));
	if (d == NULL)
		return -ENOMEM;

	hash_init(&d->table);

	ret = hash_enter(&main_catalog, name, strlen(name), &d->hobj,
			 &hash_operations);
	/*
	 * Same as cluster_init(), redo if someone slipped in,
	 * creating the cluster.
	 */
	if (ret == -EEXIST) {
		hash_destroy(&d->table);
		xnfree(d);
		goto redo;
	}

	sc->d = d;

	return syncobj_init(&d->sobj, CLOCK_COPPERPLATE,
			    SYNCOBJ_FIFO, fnref_null);
}

int syncluster_addobj(struct syncluster *sc, const char *name,
		      struct clusterobj *cobj)
{
	struct syncluster_wait_struct *wait;
	struct threadobj *thobj, *tmp;
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&sc->d->sobj, &syns);
	if (ret)
		return __bt(ret);

	cobj->cnode = __node_id;

	ret = hash_enter_probe(&sc->d->table, name, strlen(name),
			       &cobj->hobj, &hash_operations);
	if (ret)
		goto out;

	if (!syncobj_grant_wait_p(&sc->d->sobj))
		goto out;
	/*
	 * Wake up all threads waiting for this key to appear in the
	 * dictionary.
	 */
	syncobj_for_each_grant_waiter_safe(&sc->d->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		if (strcmp(__mptr(wait->name_ref), name) == 0)
			syncobj_grant_to(&sc->d->sobj, thobj);
	}
out:
	syncobj_unlock(&sc->d->sobj, &syns);

	return ret;
}

int syncluster_delobj(struct syncluster *sc,
		      struct clusterobj *cobj)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&sc->d->sobj, &syns);
	if (ret)
		return ret;

	ret = __bt(hash_remove(&sc->d->table, &cobj->hobj, &hash_operations));

	syncobj_unlock(&sc->d->sobj, &syns);

	return ret;
}

int syncluster_findobj(struct syncluster *sc,
		       const char *name,
		       const struct timespec *timeout,
		       struct clusterobj **cobjp)
{
	struct syncluster_wait_struct *wait = NULL;
	struct syncstate syns;
	struct hashobj *hobj;
	int ret = 0;

	ret = syncobj_lock(&sc->d->sobj, &syns);
	if (ret)
		return ret;

	for (;;) {
		hobj = hash_search_probe(&sc->d->table, name, strlen(name),
					 &hash_operations);
		if (hobj) {
			*cobjp = container_of(hobj, struct clusterobj, hobj);
			break;
		}
		if (timeout &&
		    timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
			ret = -EWOULDBLOCK;
			break;
		}
		if (!threadobj_current_p()) {
			ret = -EPERM;
			break;
		}
		if (wait == NULL) {
			wait = threadobj_prepare_wait(struct syncluster_wait_struct);
			wait->name_ref = __moff(xnstrdup(name));
			if (!__mptr(wait->name_ref)) {
				ret = -ENOMEM;
				break;
			}
		}
		ret = syncobj_wait_grant(&sc->d->sobj, timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}

	syncobj_unlock(&sc->d->sobj, &syns);
out:
	if (wait) {
		xnfree(__mptr(wait->name_ref));
		threadobj_finish_wait();
	}

	return ret;
}

const static struct hash_operations hash_operations = {
	.compare = memcmp,
	.probe = cluster_probe,
	.alloc = xnmalloc,
	.free = xnfree,
};

const static struct pvhash_operations pvhash_operations = {
	.compare = memcmp,
};

#else /* !CONFIG_XENO_PSHARED */

const static struct hash_operations hash_operations = {
	.compare = memcmp,
};

#endif /* !CONFIG_XENO_PSHARED */

int pvcluster_init(struct pvcluster *c, const char *name)
{
	pvhash_init(&c->table);
	return 0;
}

void pvcluster_destroy(struct pvcluster *c)
{
	/* nop */
}

int pvcluster_addobj(struct pvcluster *c, const char *name,
		     struct pvclusterobj *cobj)
{
	return pvhash_enter(&c->table, name, strlen(name), &cobj->hobj,
			    &pvhash_operations);
}

int pvcluster_addobj_dup(struct pvcluster *c, const char *name,
			 struct pvclusterobj *cobj)
{
	return pvhash_enter_dup(&c->table, name, strlen(name), &cobj->hobj,
				&pvhash_operations);
}

int pvcluster_delobj(struct pvcluster *c, struct pvclusterobj *cobj)
{
	return __bt(pvhash_remove(&c->table, &cobj->hobj, &pvhash_operations));
}

struct pvclusterobj *pvcluster_findobj(struct pvcluster *c, const char *name)
{
	struct pvhashobj *hobj;

	hobj = pvhash_search(&c->table, name, strlen(name),
			     &pvhash_operations);
	if (hobj == NULL)
		return NULL;

	return container_of(hobj, struct pvclusterobj, hobj);
}

static int __pvcluster_walk(struct pvhash_table *t, struct pvhashobj *hobj,
			    void *arg)
{
	struct pvcluster_walk_data *wd = arg;
	struct pvclusterobj *cobj;

	cobj = container_of(hobj, struct pvclusterobj, hobj);

	return wd->walk(wd->c, cobj);
}
  
int pvcluster_walk(struct pvcluster *c,
		   int (*walk)(struct pvcluster *c,
			       struct pvclusterobj *cobj))
{
	struct pvcluster_walk_data wd = {
		.c = c,
		.walk = walk,
	};
	return pvhash_walk(&c->table, __pvcluster_walk, &wd);
}

int pvsyncluster_init(struct pvsyncluster *sc, const char *name)
{
	int ret;

	ret = __bt(pvcluster_init(&sc->c, name));
	if (ret)
		return ret;

	/*
	 * Assuming pvcluster_destroy() is a nop, so we don't need to
	 * run any finalizer.
	 */
	return syncobj_init(&sc->sobj, CLOCK_COPPERPLATE,
			    SYNCOBJ_FIFO, fnref_null);
}

void pvsyncluster_destroy(struct pvsyncluster *sc)
{
	struct syncstate syns;

	if (__bt(syncobj_lock(&sc->sobj, &syns)))
		return;

	/* No finalizer, we just destroy the synchro. */
	syncobj_destroy(&sc->sobj, &syns);
}

int pvsyncluster_addobj(struct pvsyncluster *sc, const char *name,
			struct pvclusterobj *cobj)
{
	struct syncluster_wait_struct *wait;
	struct threadobj *thobj, *tmp;
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&sc->sobj, &syns);
	if (ret)
		return __bt(ret);

	ret = pvcluster_addobj(&sc->c, name, cobj);
	if (ret)
		goto out;

	if (!syncobj_grant_wait_p(&sc->sobj))
		goto out;
	/*
	 * Wake up all threads waiting for this key to appear in the
	 * dictionary.
	 */
	syncobj_for_each_grant_waiter_safe(&sc->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		if (strcmp(wait->name, name) == 0)
			syncobj_grant_to(&sc->sobj, thobj);
	}
out:
	syncobj_unlock(&sc->sobj, &syns);

	return ret;
}

int pvsyncluster_delobj(struct pvsyncluster *sc,
			struct pvclusterobj *cobj)
{
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&sc->sobj, &syns);
	if (ret)
		return ret;

	ret = __bt(pvcluster_delobj(&sc->c, cobj));

	syncobj_unlock(&sc->sobj, &syns);

	return ret;
}

int pvsyncluster_findobj(struct pvsyncluster *sc,
			 const char *name,
			 const struct timespec *timeout,
			 struct pvclusterobj **cobjp)
{
	struct syncluster_wait_struct *wait = NULL;
	struct pvclusterobj *cobj;
	struct syncstate syns;
	int ret = 0;

	ret = syncobj_lock(&sc->sobj, &syns);
	if (ret)
		return ret;

	for (;;) {
		cobj = pvcluster_findobj(&sc->c, name);
		if (cobj) {
			*cobjp = cobj;
			break;
		}
		if (timeout &&
		    timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
			ret = -EWOULDBLOCK;
			break;
		}
		if (!threadobj_current_p()) {
			ret = -EPERM;
			break;
		}
		if (wait == NULL) {
			wait = threadobj_prepare_wait(struct syncluster_wait_struct);
			wait->name = name;
		}
		ret = syncobj_wait_grant(&sc->sobj, timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}

	syncobj_unlock(&sc->sobj, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	return ret;
}
