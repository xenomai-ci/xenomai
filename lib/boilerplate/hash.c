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

#include <string.h>
#include <errno.h>

#include "boilerplate/lock.h"
#include "boilerplate/hash.h"

/*
 * Crunching routine borrowed from:
 *
 * lookup2.c, by Bob Jenkins, December 1996, Public Domain.
 * hash(), hash2(), hash3, and mix() are externally useful functions.
 * Routines to test the hash are included if SELF_TEST is defined.
 * You can use this free for any purpose.  It has no warranty.
 */

#define __mixer(a, b, c) \
	{					\
	a -= b; a -= c; a ^= (c>>13);		\
	b -= c; b -= a; b ^= (a<<8);		\
	c -= a; c -= b; c ^= (b>>13);		\
	a -= b; a -= c; a ^= (c>>12);		\
	b -= c; b -= a; b ^= (a<<16);		\
	c -= a; c -= b; c ^= (b>>5);		\
	a -= b; a -= c; a ^= (c>>3);		\
	b -= c; b -= a; b ^= (a<<10);		\
	c -= a; c -= b; c ^= (b>>15);		\
}

static inline int store_key(struct hashobj *obj,
			    const void *key, size_t len,
			    const struct hash_operations *hops);

static inline void drop_key(struct hashobj *obj,
			    const struct hash_operations *hops);

#define GOLDEN_HASH_RATIO  0x9e3779b9  /* Arbitrary value. */

unsigned int __hash_key(const void *key, size_t length, unsigned int c)
{
	const unsigned char *k = key;
	unsigned int a, b, len;

	len = (unsigned int)length;
	a = b = GOLDEN_HASH_RATIO;

	while (len >= 12) {
		a += (k[0] +((unsigned int)k[1]<<8) +((unsigned int)k[2]<<16) +((unsigned int)k[3]<<24));
		b += (k[4] +((unsigned int)k[5]<<8) +((unsigned int)k[6]<<16) +((unsigned int)k[7]<<24));
		c += (k[8] +((unsigned int)k[9]<<8) +((unsigned int)k[10]<<16)+((unsigned int)k[11]<<24));
		__mixer(a, b, c);
		k += 12;
		len -= 12;
	}

	c += (unsigned int)length;

	switch (len) {
	case 11: c += ((unsigned int)k[10]<<24);
	case 10: c += ((unsigned int)k[9]<<16);
	case 9 : c += ((unsigned int)k[8]<<8);
	case 8 : b += ((unsigned int)k[7]<<24);
	case 7 : b += ((unsigned int)k[6]<<16);
	case 6 : b += ((unsigned int)k[5]<<8);
	case 5 : b += k[4];
	case 4 : a += ((unsigned int)k[3]<<24);
	case 3 : a += ((unsigned int)k[2]<<16);
	case 2 : a += ((unsigned int)k[1]<<8);
	case 1 : a += k[0];
	};

	__mixer(a, b, c);

	return c;
}

void __hash_init(void *heap, struct hash_table *t)
{
	pthread_mutexattr_t mattr;
	int n;

	for (n = 0; n < HASHSLOTS; n++)
		__list_init(heap, &t->table[n].obj_list);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	__RT(pthread_mutex_init(&t->lock, &mattr));
	pthread_mutexattr_destroy(&mattr);
}

void hash_destroy(struct hash_table *t)
{
	__RT(pthread_mutex_destroy(&t->lock));
}

static struct hash_bucket *do_hash(struct hash_table *t,
				   const void *key, size_t len)
{
	unsigned int hash = __hash_key(key, len, 0);
	return &t->table[hash & (HASHSLOTS-1)];
}

int __hash_enter(struct hash_table *t,
		 const void *key, size_t len,
		 struct hashobj *newobj,
		 const struct hash_operations *hops,
		 int nodup)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;
	int ret;

	holder_init(&newobj->link);
	ret = store_key(newobj, key, len, hops);
	if (ret)
		return ret;

	bucket = do_hash(t, key, len);
	write_lock_nocancel(&t->lock);

	if (nodup && !list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj->len != newobj->len)
				continue;
			if (hops->compare(__mptr(obj->key), __mptr(newobj->key),
					  obj->len) == 0) {
				drop_key(newobj, hops);
				ret = -EEXIST;
				goto out;
			}
		}
	}

	list_append(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);

	return ret;
}

int hash_remove(struct hash_table *t, struct hashobj *delobj,
		const struct hash_operations *hops)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;
	int ret = -ESRCH;

	bucket = do_hash(t, __mptr(delobj->key), delobj->len);

	write_lock_nocancel(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj == delobj) {
				list_remove_init(&obj->link);
				drop_key(obj, hops);
				ret = 0;
				goto out;
			}
		}
	}
out:
	write_unlock(&t->lock);

	return __bt(ret);
}

struct hashobj *hash_search(struct hash_table *t, const void *key,
			    size_t len, const struct hash_operations *hops)
{
	struct hash_bucket *bucket;
	struct hashobj *obj;

	bucket = do_hash(t, key, len);

	read_lock_nocancel(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj->len != len)
				continue;
			if (hops->compare(__mptr(obj->key), key, len) == 0)
				goto out;
		}
	}
	obj = NULL;
out:
	read_unlock(&t->lock);

	return obj;
}

int hash_walk(struct hash_table *t, hash_walk_op walk, void *arg)
{
	struct hash_bucket *bucket;
	struct hashobj *obj, *tmp;
	int ret, n;

	read_lock_nocancel(&t->lock);

	for (n = 0; n < HASHSLOTS; n++) {
		bucket = &t->table[n];
		if (list_empty(&bucket->obj_list))
			continue;
		list_for_each_entry_safe(obj, tmp, &bucket->obj_list, link) {
			read_unlock(&t->lock);
			ret = walk(t, obj, arg);
			if (ret)
				return __bt(ret);
			read_lock_nocancel(&t->lock);
		}
	}

	read_unlock(&t->lock);

	return 0;
}

#ifdef CONFIG_XENO_PSHARED

static inline int store_key(struct hashobj *obj,
			    const void *key, size_t len,
			    const struct hash_operations *hops)
{
	void *p;
	
	assert(__mchk(obj));

	if (len > sizeof(obj->static_key)) {
		p = hops->alloc(len);
		if (p == NULL)
			return -ENOMEM;
		assert(__mchk(p));
	} else
		p = obj->static_key;

	memcpy(p, key, len);
	obj->key = __moff(p);
	obj->len = len;

	return 0;
}

static inline void drop_key(struct hashobj *obj,
			    const struct hash_operations *hops)
{
	const void *key = __mptr(obj->key);

	if (key != obj->static_key)
		hops->free((void *)key);
}

int __hash_enter_probe(struct hash_table *t,
		       const void *key, size_t len,
		       struct hashobj *newobj,
		       const struct hash_operations *hops,
		       int nodup)
{
	struct hash_bucket *bucket;
	struct hashobj *obj, *tmp;
	struct service svc;
	int ret;

	holder_init(&newobj->link);
	ret = store_key(newobj, key, len, hops);
	if (ret)
		return ret;

	bucket = do_hash(t, key, len);
	CANCEL_DEFER(svc);
	write_lock(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry_safe(obj, tmp, &bucket->obj_list, link) {
			if (obj->len != newobj->len)
				continue;
			if (hops->compare(__mptr(obj->key),
					  __mptr(newobj->key), obj->len) == 0) {
				if (hops->probe(obj)) {
					if (nodup) {
						drop_key(newobj, hops);
						ret = -EEXIST;
						goto out;
					}
					continue;
				}
				list_remove_init(&obj->link);
				drop_key(obj, hops);
			}
		}
	}

	list_append(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);
	CANCEL_RESTORE(svc);

	return ret;
}

struct hashobj *hash_search_probe(struct hash_table *t,
				  const void *key, size_t len,
				  const struct hash_operations *hops)
{
	struct hash_bucket *bucket;
	struct hashobj *obj, *tmp;
	struct service svc;

	bucket = do_hash(t, key, len);

	CANCEL_DEFER(svc);
	write_lock(&t->lock);

	if (!list_empty(&bucket->obj_list)) {
		list_for_each_entry_safe(obj, tmp, &bucket->obj_list, link) {
			if (obj->len != len)
				continue;
			if (hops->compare(__mptr(obj->key), key, len) == 0) {
				if (!hops->probe(obj)) {
					list_remove_init(&obj->link);
					drop_key(obj, hops);
					continue;
				}
				goto out;
			}
		}
	}
	obj = NULL;
out:
	write_unlock(&t->lock);
	CANCEL_RESTORE(svc);

	return obj;
}

void pvhash_init(struct pvhash_table *t)
{
	pthread_mutexattr_t mattr;
	int n;

	for (n = 0; n < HASHSLOTS; n++)
		pvlist_init(&t->table[n].obj_list);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	__RT(pthread_mutex_init(&t->lock, &mattr));
	pthread_mutexattr_destroy(&mattr);
}

static struct pvhash_bucket *do_pvhash(struct pvhash_table *t,
				       const void *key, size_t len)
{
	unsigned int hash = __hash_key(key, len, 0);
	return &t->table[hash & (HASHSLOTS-1)];
}

int __pvhash_enter(struct pvhash_table *t,
		   const void *key, size_t len,
		   struct pvhashobj *newobj,
		   const struct pvhash_operations *hops,
		   int nodup)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;
	int ret = 0;

	pvholder_init(&newobj->link);
	newobj->key = key;
	newobj->len = len;
	bucket = do_pvhash(t, key, len);

	write_lock_nocancel(&t->lock);

	if (nodup && !pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj->len != newobj->len)
				continue;
			if (hops->compare(obj->key, newobj->key, len) == 0) {
				ret = -EEXIST;
				goto out;
			}
		}
	}

	pvlist_append(&newobj->link, &bucket->obj_list);
out:
	write_unlock(&t->lock);

	return ret;
}

int pvhash_remove(struct pvhash_table *t, struct pvhashobj *delobj,
		  const struct pvhash_operations *hops)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;
	int ret = -ESRCH;

	bucket = do_pvhash(t, delobj->key, delobj->len);

	write_lock_nocancel(&t->lock);

	if (!pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj == delobj) {
				pvlist_remove_init(&obj->link);
				ret = 0;
				goto out;
			}
		}
	}
out:
	write_unlock(&t->lock);

	return __bt(ret);
}

struct pvhashobj *pvhash_search(struct pvhash_table *t,
				const void *key, size_t len,
				const struct pvhash_operations *hops)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj;

	bucket = do_pvhash(t, key, len);

	read_lock_nocancel(&t->lock);

	if (!pvlist_empty(&bucket->obj_list)) {
		pvlist_for_each_entry(obj, &bucket->obj_list, link) {
			if (obj->len != len)
				continue;
			if (hops->compare(obj->key, key, len) == 0)
				goto out;
		}
	}
	obj = NULL;
out:
	read_unlock(&t->lock);

	return obj;
}

int pvhash_walk(struct pvhash_table *t,	pvhash_walk_op walk, void *arg)
{
	struct pvhash_bucket *bucket;
	struct pvhashobj *obj, *tmp;
	int ret, n;

	read_lock_nocancel(&t->lock);

	for (n = 0; n < HASHSLOTS; n++) {
		bucket = &t->table[n];
		if (pvlist_empty(&bucket->obj_list))
			continue;
		pvlist_for_each_entry_safe(obj, tmp, &bucket->obj_list, link) {
			read_unlock(&t->lock);
			ret = walk(t, obj, arg);
			if (ret)
				return __bt(ret);
			read_lock_nocancel(&t->lock);
		}
	}

	read_unlock(&t->lock);

	return 0;
}

#else /* !CONFIG_XENO_PSHARED */

static inline int store_key(struct hashobj *obj,
			    const void *key, size_t len,
			    const struct hash_operations *hops)
{
	obj->key = key;
	obj->len = len;

	return 0;
}

static inline void drop_key(struct hashobj *obj,
			    const struct hash_operations *hops)
{ }

#endif /* !CONFIG_XENO_PSHARED */
