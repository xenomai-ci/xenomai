/*
 * Copyright (c) 2015 Gilles Chanteperdrix
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#if (!defined(_BOILERPLATE_AVL_INNER_H) && !defined(AVL_PSHARED)) || \
    (!defined(_BOILERPLATE_AVL_SHARED_INNER_H) && defined(AVL_PSHARED)) /* Yeah, well... */

#if !defined(_BOILERPLATE_AVL_H) && !defined(_BOILERPLATE_SHAVL_H)
#error "Do not include this file directly. Use <boilerplate/avl.h> or <boilerplate/shavl.h> instead."
#endif

#include <stddef.h>
#include <stdio.h>

#ifdef AVL_PSHARED
#define __AVL(__decl)	shavl_ ## __decl
#define __AVLH(__decl)	shavlh_ ## __decl
#define __AVL_T(__type)	sh ## __type
#define _BOILERPLATE_AVL_SHARED_INNER_H
#else
#define __AVL(__decl)	avl_ ## __decl
#define __AVLH(__decl)	avlh_ ## __decl
#define __AVL_T(__type)	__type
#define _BOILERPLATE_AVL_INNER_H
#endif

struct __AVL_T(avlh) {
#define AVLH_APP_BITS 28
	unsigned int flags: AVLH_APP_BITS;
	int type: 2;
	int balance: 2;
	union {
		ptrdiff_t offset;
		struct __AVL_T(avlh) *ptr;
	} link[3];
};

struct __AVL_T(avl);

/*
 * Comparison function: should return -1 if left is less than right, 0
 * if they are equal and 1 if left is greather than right. You can use
 * the avl_sign function which will convert a difference to -1, 0,
 * 1. Beware of overflow however. You can also use avl_cmp_sign()
 * which should not have any such problems.
 */
typedef int __AVL_T(avlh_cmp_t)(const struct __AVL_T(avlh) *const,
				const struct __AVL_T(avlh) *const);

typedef struct __AVL_T(avlh) *
__AVL_T(avl_search_t)(const struct __AVL_T(avl) *,
		      const struct __AVL_T(avlh) *, int *, int);

typedef int __AVL_T(avlh_prn_t)(char *, size_t,
				const struct __AVL_T(avlh) *const);

struct __AVL_T(avl_searchops) {
	__AVL_T(avl_search_t) *search;
	__AVL_T(avlh_cmp_t) *cmp;
};

struct __AVL_T(avl) {
	struct __AVL_T(avlh) anchor;
	union {
		ptrdiff_t offset;
		struct __AVL_T(avlh) *ptr;
	} end[3];
	unsigned int count;
	unsigned int height;
};

#define AVL_LEFT	     -1
#define AVL_UP		      0
#define AVL_RIGHT	      1
/* maps AVL_LEFT to AVL_RIGHT and reciprocally. */
#define avl_opposite(type)   (-(type))
/* maps AVL_LEFT and AVL_RIGHT to arrays index (or bit positions). */
#define avl_type2index(type) ((type)+1)

#define AVL_THR_LEFT  (1 << avl_type2index(AVL_LEFT))
#define AVL_THR_RIGHT (1 << avl_type2index(AVL_RIGHT))

#ifdef AVL_PSHARED

static inline struct shavlh *
shavlh_link(const struct shavl *const avl,
	    const struct shavlh *const holder, unsigned int dir)
{
	ptrdiff_t offset = holder->link[avl_type2index(dir)].offset;
	return offset == (ptrdiff_t)-1 ? NULL : (void *)avl + offset;
}

static inline void
shavlh_set_link(struct shavl *const avl, struct shavlh *lhs,
		int dir, struct shavlh *rhs)
{
	ptrdiff_t offset = rhs ? (void *)rhs - (void *)avl : (ptrdiff_t)-1;
	lhs->link[avl_type2index(dir)].offset = offset;
}

static inline
struct shavlh *shavl_end(const struct shavl *const avl, int dir)
{
	ptrdiff_t offset = avl->end[avl_type2index(dir)].offset;
	return offset == (ptrdiff_t)-1 ? NULL : (void *)avl + offset;
}

static inline void
shavl_set_end(struct shavl *const avl, int dir, struct shavlh *holder)
{
	ptrdiff_t offset = holder ? (void *)holder - (void *)avl : (ptrdiff_t)-1;
	avl->end[avl_type2index(dir)].offset = offset;
}

#define shavl_count(avl)	((avl)->count)
#define shavl_height(avl)	((avl)->height)
#define shavl_anchor(avl)	(&(avl)->anchor)

#define shavlh_up(avl, holder)			\
	shavlh_link((avl), (holder), AVL_UP)
#define shavlh_left(avl, holder)		\
	shavlh_link((avl), (holder), AVL_LEFT)
#define shavlh_right(avl, holder)		\
	shavlh_link((avl), (holder), AVL_RIGHT)

#define shavlh_thr_tst(avl, holder, side)	\
	(shavlh_link(avl, holder, side) == NULL)
#define shavlh_child(avl, holder, side)		\
	(shavlh_link((avl),(holder),(side)))
#define shavlh_has_child(avl, holder, side)	\
	(!shavlh_thr_tst(avl, holder, side))

#define shavl_top(avl)	  (shavlh_right(avl, shavl_anchor(avl)))
#define shavl_head(avl)	  (shavl_end((avl), AVL_LEFT))
#define shavl_tail(avl)	  (shavl_end((avl), AVL_RIGHT))

/*
 * Search a node in a pshared AVL, return its parent if it could not
 * be found.
 */
#define DECLARE_SHAVL_SEARCH(__search_fn, __cmp)			\
	struct shavlh *__search_fn(const struct shavl *const avl,	\
				   const struct shavlh *const node,	\
				   int *const pdelta, int dir)		\
	{								\
		int delta = AVL_RIGHT;					\
		struct shavlh *holder = shavl_top(avl), *next;		\
									\
		if (holder == NULL)					\
			goto done;					\
									\
		for (;;) {						\
			delta = __cmp(node, holder);			\
			/*						\
			 * Handle duplicates keys here, according to	\
			 * "dir", if dir is:				\
			 * - AVL_LEFT, the leftmost node is returned,	\
			 * - AVL_RIGHT, the rightmost node is returned,	\
			 * - 0, the first match is returned.		\
			 */						\
			if (!(delta ?: dir))				\
				break;					\
			next = shavlh_child(avl, holder, delta ?: dir); \
			if (next == NULL)				\
				break;					\
			holder = next;					\
		}							\
									\
	  done:								\
		*pdelta = delta;					\
		return holder;						\
	}

#else  /* !AVL_PSHARED */

#define avlh_link(avl, holder, dir) ((holder)->link[avl_type2index(dir)].ptr)

#define avl_end(avl, dir) ((avl)->end[avl_type2index(dir)].ptr)

static inline void
avlh_set_link(struct avl *const avl, struct avlh *lhs, int dir, struct avlh *rhs)
{
	avlh_link(avl, lhs, dir) = rhs;
}

static inline void
avl_set_end(struct avl *const avl, int dir, struct avlh *holder)
{
	avl_end(avl, dir) = holder;
}

#define avl_count(avl)	  ((avl)->count)
#define avl_height(avl)	  ((avl)->height)
#define avl_anchor(avl)	  (&(avl)->anchor)

#define avlh_up(avl, holder)	avlh_link((avl), (holder), AVL_UP)
#define avlh_left(avl, holder)	avlh_link((avl), (holder), AVL_LEFT)
#define avlh_right(avl, holder)	avlh_link((avl), (holder), AVL_RIGHT)

#define avlh_thr_tst(avl, holder, side) (avlh_link(avl, holder, side) == NULL)
#define avlh_child(avl, holder, side) (avlh_link((avl),(holder),(side)))
#define avlh_has_child(avl, holder, side) (!avlh_thr_tst(avl, holder, side))

#define avl_top(avl)	  (avlh_right(avl, avl_anchor(avl)))
#define avl_head(avl)	  (avl_end((avl), AVL_LEFT))
#define avl_tail(avl)	  (avl_end((avl), AVL_RIGHT))

/*
 * Search a node in a private AVL, return its parent if it could not
 * be found.
 */
#define DECLARE_AVL_SEARCH(__search_fn, __cmp)				\
	struct avlh *__search_fn(const struct avl *const avl,		\
				 const struct avlh *const node,		\
				 int *const pdelta, int dir)		\
	{								\
		int delta = AVL_RIGHT;					\
		struct avlh *holder = avl_top(avl), *next;		\
									\
		if (holder == NULL)					\
			goto done;					\
									\
		for (;;) {						\
			delta = __cmp(node, holder);			\
			/*						\
			 * Handle duplicates keys here, according to	\
			 * "dir", if dir is:				\
			 * - AVL_LEFT, the leftmost node is returned,	\
			 * - AVL_RIGHT, the rightmost node is returned,	\
			 * - 0, the first match is returned.		\
			 */						\
			if (!(delta ?: dir))				\
				break;					\
			next = avlh_child(avl, holder, delta ?: dir);	\
			if (next == NULL)				\
				break;					\
			holder = next;					\
		}							\
									\
	  done:								\
		*pdelta = delta;					\
		return holder;						\
	}

#endif	/* !AVL_PSHARED */

/*
 * From "Bit twiddling hacks", returns v < 0 ? -1 : (v > 0 ? 1 : 0)
 */
#define avl_sign(v)				\
	({					\
		typeof(v) _v = (v);		\
		((_v) > 0) - ((_v) < 0);	\
	})

/*
 * Variation on the same theme.
 */
#define avl_cmp_sign(l, r)			\
	({					\
		typeof(l) _l = (l);		\
		typeof(r) _r = (r);		\
		(_l > _r) - (_l < _r);		\
	})

static inline struct __AVL_T(avlh) *
__AVL(search_inner)(const struct __AVL_T(avl) *const avl,
		    const struct __AVL_T(avlh) *n, int *delta,
		    const struct __AVL_T(avl_searchops) *ops)
{
	return ops->search(avl, n, delta, 0);
}

static inline
struct __AVL_T(avlh) *__AVL(gettop)(const struct __AVL_T(avl) *const avl)
{
	return __AVL(top)(avl);
}

static inline
struct __AVL_T(avlh) *__AVL(gethead)(const struct __AVL_T(avl) *const avl)
{
	return __AVL(head)(avl);
}

static inline
struct __AVL_T(avlh) *__AVL(gettail)(const struct __AVL_T(avl) *const avl)
{
	return __AVL(tail)(avl);
}

static inline
unsigned int __AVL(getcount)(const struct __AVL_T(avl) *const avl)
{
	return __AVL(count)(avl);
}

struct __AVL_T(avlh) *__AVL(inorder)(const struct __AVL_T(avl) *const avl,
				     struct __AVL_T(avlh) *holder,
				     const int dir);

struct __AVL_T(avlh) *__AVL(postorder)(const struct __AVL_T(avl) *const avl,
				       struct __AVL_T(avlh) *const holder,
				       const int dir);

struct __AVL_T(avlh) *__AVL(preorder)(const struct __AVL_T(avl) *const avl,
				      struct __AVL_T(avlh) *holder,
				      const int dir);

static inline struct __AVL_T(avlh) *
__AVL(next)(const struct __AVL_T(avl) *const avl,
	    struct __AVL_T(avlh) *const holder)
{
	return __AVL(inorder)(avl, holder, AVL_RIGHT);
}

static inline struct __AVL_T(avlh) *
__AVL(prev)(const struct __AVL_T(avl) *const avl,
	    struct __AVL_T(avlh) *const holder)
{
	return __AVL(inorder)(avl, holder, AVL_LEFT);
}

static inline struct __AVL_T(avlh) *
__AVL(postorder_next)(const struct __AVL_T(avl) *const avl,
		      struct __AVL_T(avlh) *const holder)
{
	return __AVL(postorder)(avl, holder, AVL_RIGHT);
}

static inline struct __AVL_T(avlh) *
__AVL(postorder_prev)(const struct __AVL_T(avl) *const avl,
		      struct __AVL_T(avlh) *const holder)
{
	return __AVL(postorder)(avl, holder, AVL_LEFT);
}

static inline struct __AVL_T(avlh) *
__AVL(preorder_next)(const struct __AVL_T(avl) *const avl,
		     struct __AVL_T(avlh) *const holder)
{
	return __AVL(preorder)(avl, holder, AVL_RIGHT);
}

static inline struct __AVL_T(avlh) *
__AVL(preorder_prev)(const struct __AVL_T(avl) *const avl,
		     struct __AVL_T(avlh) *const holder)
{
	return __AVL(preorder)(avl, holder, AVL_LEFT);
}

static inline void __AVLH(init)(struct __AVL_T(avlh) *const holder)
{
	holder->balance = 0;
	holder->type = 0;
}

static inline struct __AVL_T(avlh) *
__AVL(search)(const struct __AVL_T(avl) *const avl,
	      const struct __AVL_T(avlh) *node,
	      const struct __AVL_T(avl_searchops) *ops)
{
	struct __AVL_T(avlh) *holder;
	int delta;

	holder = __AVL(search_inner)(avl, node, &delta, ops);
	if (!delta)
		return holder;

	return NULL;
}

static inline struct __AVL_T(avlh) *
__AVL(search_nearest)(const struct __AVL_T(avl) *const avl,
		      const struct __AVL_T(avlh) *node, int dir,
		      const struct __AVL_T(avl_searchops) *ops)
{
	struct __AVL_T(avlh) *holder;
	int delta;

	holder = __AVL(search_inner)(avl, node, &delta, ops);
	if (!holder || delta != dir)
		return holder;

	return __AVL(inorder)(avl, holder, dir);
}

static inline struct __AVL_T(avlh) *
__AVL(search_le)(const struct __AVL_T(avl) *const avl,
		 const struct __AVL_T(avlh) *node,
		 const struct __AVL_T(avl_searchops) *ops)
{
	return __AVL(search_nearest)(avl, node, AVL_LEFT, ops);
}

static inline struct __AVL_T(avlh) *
__AVL(search_ge)(const struct __AVL_T(avl) *const avl,
		 const struct __AVL_T(avlh) *node,
		 const struct __AVL_T(avl_searchops) *ops)
{
	return __AVL(search_nearest)(avl, node, AVL_RIGHT, ops);
}

static inline struct __AVL_T(avlh) *
__AVL(search_multi)(const struct __AVL_T(avl) *const avl,
		    const struct __AVL_T(avlh) *node, int dir,
		    const struct __AVL_T(avl_searchops) *ops)
{
	struct __AVL_T(avlh) *holder;
	int delta;

	holder = ops->search(avl, node, &delta, dir);
	if (!delta)
		return holder;

	if (!holder)
		return NULL;

	return __AVL(inorder)(avl, holder, -dir);
}

static inline struct __AVL_T(avlh) *
__AVL(search_first)(const struct __AVL_T(avl) *const avl,
		    const struct __AVL_T(avlh) *node,
		    const struct __AVL_T(avl_searchops) *ops)
{
	return __AVL(search_multi)(avl, node, AVL_LEFT, ops);
}

static inline struct __AVL_T(avlh) *
__AVL(search_last)(const struct __AVL_T(avl) *const avl,
		   const struct __AVL_T(avlh) *node,
		   const struct __AVL_T(avl_searchops) *ops)
{
	return __AVL(search_multi)(avl, node, AVL_RIGHT, ops);
}

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void __AVL(init)(struct __AVL_T(avl) *const avl);
  
void __AVL(destroy)(struct __AVL_T(avl) *const avl);

int __AVL(insert)(struct __AVL_T(avl) *const avl,
		  struct __AVL_T(avlh) *const holder,
		  const struct __AVL_T(avl_searchops) *ops);
	
int __AVL(insert_front)(struct __AVL_T(avl) *avl,
			struct __AVL_T(avlh) *holder,
			const struct __AVL_T(avl_searchops) *ops);

int __AVL(insert_back)(struct __AVL_T(avl) *avl,
		       struct __AVL_T(avlh) *holder,
		       const struct __AVL_T(avl_searchops) *ops);

int __AVL(insert_at)(struct __AVL_T(avl) *const avl,
		     struct __AVL_T(avlh) *parent, int dir,
		     struct __AVL_T(avlh) *child);

int __AVL(prepend)(struct __AVL_T(avl) *const avl,
		   struct __AVL_T(avlh) *const holder,
		   const struct __AVL_T(avl_searchops) *ops);

int __AVL(append)(struct __AVL_T(avl) *const avl,
		  struct __AVL_T(avlh) *const holder,
		  const struct __AVL_T(avl_searchops) *ops);
	
int __AVL(delete)(struct __AVL_T(avl) *const avl,
		  struct __AVL_T(avlh) *node);

int __AVL(replace)(struct __AVL_T(avl) *avl,
		   struct __AVL_T(avlh) *oldh,
		   struct __AVL_T(avlh) *newh,
		   const struct __AVL_T(avl_searchops) *ops);

struct __AVL_T(avlh) *__AVL(update)(struct __AVL_T(avl) *const avl,
				    struct __AVL_T(avlh) *const holder,
				    const struct __AVL_T(avl_searchops) *ops);

struct __AVL_T(avlh) *__AVL(set)(struct __AVL_T(avl) *const avl,
				 struct __AVL_T(avlh) *const holder,
				 const struct __AVL_T(avl_searchops) *ops);

void __AVL(clear)(struct __AVL_T(avl) *const avl,
		  void (*destruct)(struct __AVL_T(avlh) *));

int __AVL(check)(const struct __AVL_T(avl) *avl,
		 const struct __AVL_T(avl_searchops) *ops);
	
void __AVL(dump)(FILE *file, const struct __AVL_T(avl) *const avl,
		 __AVL_T(avlh_prn_t) *prn, unsigned int indent,
		 unsigned int len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef __AVL
#undef __AVLH
#undef __AVL_T

#endif /* !_BOILERPLATE_AVL_INNER_H */
