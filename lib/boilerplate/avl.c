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

#include <errno.h>
#include <memory.h>

#ifdef AVL_PSHARED
#define __AVL(__decl)	shavl_ ## __decl
#define __AVLH(__decl)	shavlh_ ## __decl
#define __AVL_T(__type)	sh ## __type
#else
#define __AVL(__decl)	avl_ ## __decl
#define __AVLH(__decl)	avlh_ ## __decl
#define __AVL_T(__type)	__type
#endif

struct __AVL_T (avlh) * __AVL(inorder)(const struct __AVL_T(avl) * const avl,
				       struct __AVL_T(avlh) * holder,
				       const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct __AVL_T (avlh) * next;

	/*
	 * If the current node is not right threaded, then go down left,
	 * starting from its right child.
	 */
	if (__AVLH(has_child)(avl, holder, dir)) {
		const int opp_dir = avl_opposite(dir);
		holder = __AVLH(link)(avl, holder, dir);
		while ((next = __AVLH(child)(avl, holder, opp_dir)))
			holder = next;
		next = holder;
	} else {
		for (;;) {
			next = __AVLH(up)(avl, holder);
			if (next == __AVL(anchor)(avl))
				return NULL;
			if (holder->type != dir)
				break;
			holder = next;
		}
	}

	return next;
}

struct __AVL_T (avlh) * __AVL(postorder)(const struct __AVL_T(avl) * const avl,
					 struct __AVL_T(avlh) * const holder,
					 const int dir)
{
	/* Assume dir == AVL_RIGHT in comments. */
	struct __AVL_T (avlh) * next = __AVLH(up)(avl, holder);

	if (holder->type != dir)
		/*
		 * If the current node is not a right node, follow the nodes in
		 * inorder until we find a right threaded node.
		 */
		while (__AVLH(has_child)(avl, next, dir))
			next = __AVL(inorder)(avl, next, dir);
	else
		/*
		 * else the current node is a right node, its parent is the
		 * next in postorder.
		 */
		if (next == __AVL(anchor)(avl))
			next = NULL;

	return next;
}

struct __AVL_T (avlh) * __AVL(preorder)(const struct __AVL_T(avl) * const avl,
					struct __AVL_T(avlh) * holder,
					const int dir)
{
	struct __AVL_T (avlh) * next;
	/* Assume dir == AVL_RIGHT in comments. */
	/*
	 * If the current node has a left child (hence is not left threaded),
	 * then return it.
	 */

	if (__AVLH(has_child)(avl, holder, avl_opposite(dir)))
		return __AVLH(link)(avl, holder, avl_opposite(dir));

	/*
	 * Else follow the right threads until we find a node which is not right
	 * threaded (hence has a right child) and return its right child.
	 */
	next = holder;

	while (!__AVLH(has_child)(avl, next, dir)) {
		next = __AVL(inorder)(avl, next, dir);
		if (next == NULL)
			return NULL;
	}

	return __AVLH(link)(avl, next, dir);
}

static inline unsigned int avlh_thr(const struct __AVL_T (avl) * const avl,
				    const struct __AVL_T (avlh) * h)
{
	unsigned int result = 0;

	if (__AVLH(link)(avl, h, AVL_LEFT) == NULL)
		result |= AVL_THR_LEFT;
	if (__AVLH(link)(avl, h, AVL_RIGHT) == NULL)
		result |= AVL_THR_RIGHT;

	return result;
}

static inline void
avlh_set_parent_link(struct __AVL_T (avl) * const avl,
		     struct __AVL_T (avlh) * lhs, struct __AVL_T (avlh) * rhs)
{
	__AVLH(set_link)(avl, __AVLH(up)(avl, lhs), lhs->type, rhs);
}

static inline void
avlh_set_left(struct __AVL_T (avl) * const avl, struct __AVL_T (avlh) * lhs,
	      struct __AVL_T (avlh) * rhs)
{
	__AVLH(set_link)(avl, lhs, AVL_LEFT, rhs);
}

static inline void
avlh_set_up(struct __AVL_T (avl) * const avl, struct __AVL_T (avlh) * lhs,
	    struct __AVL_T (avlh) * rhs)
{
	__AVLH(set_link)(avl, lhs, AVL_UP, rhs);
}

static inline void
avlh_set_right(struct __AVL_T (avl) * const avl, struct __AVL_T (avlh) * lhs,
	       struct __AVL_T (avlh) * rhs)
{
	__AVLH(set_link)(avl, lhs, AVL_RIGHT, rhs);
}

static inline void avl_set_top(struct __AVL_T (avl) * const avl,
			       struct __AVL_T (avlh) * holder)
{
	__AVLH(set_link)(avl, __AVL(anchor)(avl), AVL_RIGHT, holder);
}

static inline void avl_set_head(struct __AVL_T (avl) * const avl,
				struct __AVL_T (avlh) * holder)
{
	__AVL(set_end)(avl, AVL_LEFT, holder);
}

static inline void avl_set_tail(struct __AVL_T (avl) * const avl,
				struct __AVL_T (avlh) * holder)
{
	__AVL(set_end)(avl, AVL_RIGHT, holder);
}

/* Internal functions used for rebalancing (for insertion and deletion). */
static inline struct __AVL_T (avlh) *
	avlh_rotate(struct __AVL_T (avl) * const avl,
		    struct __AVL_T (avlh) * const holder, const int dir)
{
	const int opp_dir = avl_opposite(dir);
	struct __AVL_T (avlh) * const nexttop =
		__AVLH(link)(avl, holder, opp_dir);
	struct __AVL_T (avlh) * const subtree =
		__AVLH(child)(avl, nexttop, dir);

	if (subtree) {
		__AVLH(set_link)(avl, holder, opp_dir, subtree);
		avlh_set_up(avl, subtree, holder);
		subtree->type = opp_dir;
	} else
		__AVLH(set_link)(avl, holder, opp_dir, NULL);

	__AVLH(set_link)(avl, nexttop, dir, holder);
	avlh_set_up(avl, nexttop, __AVLH(up)(avl, holder));
	nexttop->type = holder->type;
	avlh_set_up(avl, holder, nexttop);
	holder->type = dir;

	avlh_set_parent_link(avl, nexttop, nexttop);

	return nexttop;
}

static inline struct __AVL_T (avlh) *
	avlh_dbl_rotate(struct __AVL_T (avl) * const avl,
			struct __AVL_T (avlh) * const holder, const int dir)
{
	const int opp = avl_opposite(dir);

	avlh_rotate(avl, __AVLH(link)(avl, holder, opp), opp);
	return avlh_rotate(avl, holder, dir);
}

static struct __AVL_T (avlh) *
	avlh_rebalance(struct __AVL_T (avl) * const avl, struct __AVL_T (avlh) * holder,
		       const int delta)
{

	int dir = delta;
	struct __AVL_T (avlh) * const heavy_side =
		__AVLH(link)(avl, holder, dir);

	if (heavy_side->balance == -delta) {
		/* heavy_side->balance == -delta, double rotation needed. */
		holder = avlh_dbl_rotate(avl, holder, avl_opposite(dir));

		/*
		 * recompute balances, there are three nodes involved, two of
		 * which balances become null.
		 */
		dir = holder->balance ? : AVL_RIGHT;
		__AVLH(link)(avl, holder, dir)->balance = 0;
		__AVLH(link)(avl, holder, avl_opposite(dir))->balance
			= -holder->balance;
		holder->balance = 0;
	} else {
		/*
		 * heavy_side->balance == delta or 0, simple rotation needed.
		 * the case 0 occurs only when deleting, never when inserting.
		 */

		/* heavy_side becomes the new root. */
		avlh_rotate(avl, holder, avl_opposite(dir));

		/* recompute balances. */
		holder->balance -= heavy_side->balance;
		heavy_side->balance -= delta;

		holder = heavy_side;
	}
	return holder;
}

/*
 * The avlh_rebalance functions was split in two parts to allow inlining in
 * the simplest case.
 */
static inline struct __AVL_T (avlh) *
	avlh_balance_add(struct __AVL_T (avl) * const avl,
			 struct __AVL_T (avlh) * const holder, const int delta)
{
	if (holder->balance == delta)
		/* we need to rebalance the current subtree. */
		return avlh_rebalance(avl, holder, delta);

	/* the current subtree does not need rebalancing */
	holder->balance += delta;
	return holder;
}

static inline void
avlh_link_child(struct __AVL_T (avl) * const avl,
		struct __AVL_T (avlh) * const oldh,
		struct __AVL_T (avlh) * const newh, const int side)
{
	struct __AVL_T (avlh) * const child = __AVLH(link)(avl, oldh, side);

	__AVLH(set_link)(avl, newh, side, child);
	if (__AVLH(has_child)(avl, oldh, side))
		avlh_set_up(avl, child, newh);
}

static inline void
avlh_replace(struct __AVL_T (avl) * const avl,
	     struct __AVL_T (avlh) * const oldh,
	     struct __AVL_T (avlh) * const newh)
{
	newh->type = oldh->type;
	/* Do not update the balance, this has to be done by the caller. */

	avlh_set_up(avl, newh, __AVLH(up)(avl, oldh));
	avlh_set_parent_link(avl, oldh, newh);

	avlh_link_child(avl, oldh, newh, AVL_LEFT);
	avlh_link_child(avl, oldh, newh, AVL_RIGHT);
}

/* Deletion helpers. */
static void avl_delete_leaf(struct __AVL_T (avl) * const avl,
			    struct __AVL_T (avlh) * const node)
{
	/*
	 * Node has no child at all. It disappears and its father becomes
	 * threaded on the side id was.
	 */

	struct __AVL_T (avlh) * const new_node = __AVLH(up)(avl, node);
	const int dir = node->type;

	/* Suppress node. */
	__AVLH(set_link)(avl, new_node, dir, __AVLH(link)(avl, node, dir));

	if (node == __AVL(end)(avl, dir))
		__AVL(set_end)(avl, dir, new_node);
}

static struct __AVL_T (avlh) * avl_delete_1child(struct __AVL_T (avl) *
						 const avl,
						 struct __AVL_T (avlh) *
						 const node, const int dir)
{
	/*
	 * Node is threaded on one side and has a child on the other
	 * side. In this case, node is replaced by its child.
	 */

	struct __AVL_T (avlh) * const new_node = __AVLH(link)(avl, node, dir);

	/*
	 * Change links as if new_node was suppressed before calling
	 * avlh_replace.
	 */
	__AVLH(set_link)(avl, node, dir, __AVLH(link)(avl, new_node, dir));
	avlh_replace(avl, node, new_node);

	if (node == __AVL(end)(avl, avl_opposite(dir)))
		__AVL(set_end)(avl, avl_opposite(dir), new_node);
	/* new_node->balance 0, which is correct. */
	return new_node;
}

static int avl_delete_2children(struct __AVL_T (avl) * const avl,
				struct __AVL_T (avlh) * const node);

/* Insertion helpers. */
static inline void
avlh_attach(struct __AVL_T (avl) * const avl,
	    struct __AVL_T (avlh) * const parent,
	    struct __AVL_T (avlh) * const child, const int side)
{
	avlh_set_left(avl, child, NULL);
	avlh_set_right(avl, child, NULL);
	avlh_set_up(avl, child, parent);
	__AVLH(set_link)(avl, parent, side, child);
	child->type = side;
}

/*
 * Insert a node, given its parent and the side where it should be inserted.
 * Helper for all insertion functions.
 */
static inline void avl_insert_inner(struct __AVL_T (avl) * const avl,
				    struct __AVL_T (avlh) * parent,
				    struct __AVL_T (avlh) * const node,
				    const int side)
{
	avlh_attach(avl, parent ? : __AVL(anchor)(avl), node, side);
	++__AVL(count)(avl);

	if (parent == NULL)
		goto insert_first_and_ret;	/* Get away from fast path */

	if (parent == __AVL(end)(avl, side))
		__AVL(set_end)(avl, side, node);

	parent->balance += side;

	while (parent->balance) {
		const int delta = parent->type;
		parent = __AVLH(up)(avl, parent);
		if (parent == __AVL(anchor)(avl))
			goto inc_height_and_ret;	/* Get away from fast path */
		parent = avlh_balance_add(avl, parent, delta);
	}

	return;

insert_first_and_ret:
	avl_set_head(avl, node);
	avl_set_tail(avl, node);
inc_height_and_ret:
	++__AVL(height)(avl);
}

/* External functions. */
int __AVL(delete)(struct __AVL_T(avl) * const avl, struct __AVL_T(avlh) * node)
{
	if (!--__AVL(count)(avl)) {
		goto delete_last_and_ret;
	}

	switch (avlh_thr(avl, node)) {
	case (AVL_THR_LEFT | AVL_THR_RIGHT):	/* thr is 5 */
		avl_delete_leaf(avl, node);
		break;

	case AVL_THR_LEFT:	/* only AVL_LEFT bit is on, thr is 1. */
		node = avl_delete_1child(avl, node, AVL_RIGHT);
		break;

	case AVL_THR_RIGHT:	/* only AVL_RIGHT bit is on, thr is 4. */
		node = avl_delete_1child(avl, node, AVL_LEFT);
		break;

	case 0:
		return avl_delete_2children(avl, node);
	}

	/* node is the first node which needs to be rebalanced.
	   The tree is rebalanced, and contrarily to what happened for insertion,
	   the rebalancing stops when a node which is NOT balanced is met. */
	while (!node->balance) {
		const int delta = -node->type;
		node = __AVLH(up)(avl, node);
		if (node == __AVL(anchor)(avl))
			goto dec_height_and_ret;
		node = avlh_balance_add(avl, node, delta);
	}

	return 0;

delete_last_and_ret:
	avl_set_top(avl, NULL);
	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
dec_height_and_ret:
	--__AVL(height)(avl);
	return 0;
}

static int avl_delete_2children(struct __AVL_T (avl) * const avl,
				struct __AVL_T (avlh) * const node)
{
	const int dir = node->balance ? node->balance : 1;
	struct __AVL_T (avlh) * const new_node =
		__AVL(inorder)(avl, node, dir);
	__AVL(delete)(avl, new_node);
	++__AVL(count)(avl);
	avlh_replace(avl, node, new_node);
	new_node->balance = node->balance;
	if (__AVL(end)(avl, dir) == node)
		__AVL(set_end)(avl, dir, new_node);
	return 0;
}

int __AVL(prepend)(struct __AVL_T(avl) * const avl,
		   struct __AVL_T(avlh) * const holder,
		   const struct __AVL_T(avl_searchops) * ops)
{
	struct __AVL_T (avlh) * const parent = __AVL(head)(avl);
	int type = parent == NULL ? AVL_RIGHT : AVL_LEFT;

	if (parent == NULL || ops->cmp(holder, parent) < 0) {
		avl_insert_inner(avl, parent, holder, type);
		return 0;
	}

	return -EINVAL;
}

int __AVL(insert_at)(struct __AVL_T(avl) * const avl,
		     struct __AVL_T(avlh) * parent, int dir,
		     struct __AVL_T(avlh) * child)
{
	if (parent == NULL)
		dir = AVL_RIGHT;
	else {
		if (!__AVLH(thr_tst)(avl, parent, dir))
			return -EINVAL;
	}

	avl_insert_inner(avl, parent, child, dir);
	return 0;
}

int __AVL(insert)(struct __AVL_T(avl) * const avl,
		  struct __AVL_T(avlh) * const holder,
		  const struct __AVL_T(avl_searchops) * ops)
{
	int delta;
	struct __AVL_T (avlh) * parent;

	parent = __AVL(search_inner)(avl, holder, &delta, ops);
	if (delta == 0)
		return -EBUSY;

	avl_insert_inner(avl, parent, holder, delta);

	return 0;
}

int __AVL(insert_front)(struct __AVL_T(avl) * const avl,
			struct __AVL_T(avlh) * const holder,
			const struct __AVL_T(avl_searchops) * ops)
{
	int delta;
	struct __AVL_T (avlh) * parent;

	parent = ops->search(avl, holder, &delta, AVL_LEFT);

	avl_insert_inner(avl, parent, holder, delta ? : AVL_LEFT);
	return 0;
}

int __AVL(insert_back)(struct __AVL_T(avl) * const avl,
		       struct __AVL_T(avlh) * const holder,
		       const struct __AVL_T(avl_searchops) * ops)
{
	int delta;
	struct __AVL_T (avlh) * parent;

	parent = ops->search(avl, holder, &delta, AVL_RIGHT);

	avl_insert_inner(avl, parent, holder, delta ? : AVL_RIGHT);
	return 0;
}

int __AVL(append)(struct __AVL_T(avl) * const avl,
		  struct __AVL_T(avlh) * const holder,
		  const struct __AVL_T(avl_searchops) * ops)
{
	struct __AVL_T (avlh) * const parent = __AVL(tail)(avl);

	if (parent == NULL || ops->cmp(holder, parent) > 0) {
		avl_insert_inner(avl, parent, holder, AVL_RIGHT);
		return 0;
	}

	return -EINVAL;
}

/*
 * Special case, when we know that replacing a node with another will not change
 * the avl, much faster than remove + add
 */
int __AVL(replace)(struct __AVL_T(avl) * avl, struct __AVL_T(avlh) * oldh,
		   struct __AVL_T(avlh) * newh,
		   const struct __AVL_T(avl_searchops) * ops)
{
	struct __AVL_T (avlh) * prev, *next;

	prev = __AVL(prev)(avl, oldh);
	next = __AVL(next)(avl, oldh);

	if ((prev && ops->cmp(newh, prev) < 0)
	    || (next && ops->cmp(newh, next) > 0))
		return -EINVAL;

	avlh_replace(avl, oldh, newh);
	if (oldh == __AVL(head)(avl))
		avl_set_head(avl, newh);
	if (oldh == __AVL(tail)(avl))
		avl_set_tail(avl, newh);
	newh->balance = oldh->balance;
	return 0;
}

struct __AVL_T (avlh) * __AVL(update)(struct __AVL_T(avl) * const avl,
				      struct __AVL_T(avlh) * const holder,
				      const struct __AVL_T(avl_searchops) * ops)
{
	int delta;
	struct __AVL_T (avlh) * const oldh =
		__AVL(search_inner)(avl, holder, &delta, ops);

	if (!delta) {
		__AVL(replace)(avl, oldh, holder, ops);
		return oldh;
	}

	return NULL;
}

struct __AVL_T (avlh) * __AVL(set)(struct __AVL_T(avl) * const avl,
				   struct __AVL_T(avlh) * const holder,
				   const struct __AVL_T(avl_searchops) * ops)
{
	int delta;
	struct __AVL_T (avlh) * const oldh =
		__AVL(search_inner)(avl, holder, &delta, ops);

	if (delta) {
		avl_insert_inner(avl, oldh, holder, delta);
		return NULL;
	}

	__AVL(replace)(avl, oldh, holder, ops);
	return oldh;
}

void __AVL(init)(struct __AVL_T(avl) * const avl)
{
	__AVLH(init)(__AVL(anchor)(avl));	/* this must be first. */
	__AVL(height)(avl) = 0;
	__AVL(count)(avl) = 0;
	avl_set_top(avl, NULL);

	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
}

void __AVL(destroy)(struct __AVL_T(avl) * const avl)
{
	__AVL(init)(avl);
}

void __AVL(clear)(struct __AVL_T(avl) * const avl,
		  void (*destruct)(struct __AVL_T(avlh) *))
{
	if (destruct) {
		struct __AVL_T (avlh) * next, *holder = __AVL(gethead)(avl);

		while (holder) {
			next = __AVL(postorder_next)(avl, holder);
			destruct(holder);
			holder = next;
		}
	}

	__AVL(init)(avl);
}

static inline
void avl_dumper_visit(FILE * file, const struct __AVL_T (avl) * const avl,
		      struct __AVL_T (avlh) * node,
		      __AVL_T(avlh_prn_t) * prn, const char *blank,
		      unsigned int blank_sz, char *buf,
		      unsigned int indent, unsigned int len)
{
	char bal;

	if (__AVLH(has_child)(avl, node, AVL_RIGHT)) {
		if (blank_sz >= (unsigned int)(buf - blank)) {
			snprintf(buf, len + 3, "%*s\n", (int)len + 1, "bug!");
			fputs(buf - blank_sz, file);
		} else
			avl_dumper_visit(file, avl,
					 __AVLH(right)(avl, node),
					 prn, blank,
					 blank_sz + indent, buf, indent, len);
	}

	switch (node->balance) {
	case 0:
		bal = '.';
		break;
	case -1:
		bal = '-';
		break;
	case 1:
		bal = '+';
		break;
	default:
		bal = '?';	/* Bug. */
	}

	(*prn)(buf, len + 1, node);
	buf[len] = bal;
	buf[len + 1] = '\n';
	buf[len + 2] = '\0';

	fputs(buf - blank_sz, file);

	if (__AVLH(has_child)(avl, node, AVL_LEFT)) {
		if (blank_sz >= (unsigned int)(buf - blank)) {
			snprintf(buf, len + 3, "%*s\n", (int)len + 1, "bug!");
			fputs(buf - blank_sz, file);
		} else
			avl_dumper_visit(file, avl,
					 __AVLH(left)(avl, node),
					 prn, blank,
					 blank_sz + indent, buf, indent, len);
	}
}

void __AVL(dump)(FILE * file, const struct __AVL_T(avl) * const avl,
		 __AVL_T(avlh_prn_t) * prn, unsigned int indent,
		 unsigned int len)
{

	struct __AVL_T (avlh) * holder = __AVL(gettop)(avl);

	putc('\n', file);
	if (!holder)
		fputs("Empty.\n", file);
	else {
		size_t blank_sz = (__AVL(height)(avl) - 1) * indent;
		char buffer[blank_sz + len + 3];
		/* 3 == balance char + sizeof("\n\0") */
		memset(buffer, ' ', blank_sz);

		avl_dumper_visit(file, avl, holder, prn, buffer, 0,
				 buffer + blank_sz, indent, len);
	}
	fflush(file);
}

static int avl_check_visit(const struct __AVL_T (avl) * avl,
			   struct __AVL_T (avlh) * node, unsigned int level)
{
	int err;

	if (!__AVLH(has_child)(avl, node, AVL_RIGHT))
		goto check_balance;

	if (level > __AVL(height)(avl)) {
		fprintf(stderr, "too much recursion\n");
		return -EINVAL;
	}

	err = avl_check_visit(avl, __AVLH(right)(avl, node), level + 1);
	if (err < 0)
		return err;

check_balance:
	switch (node->balance) {
	case 0:
		break;
	case -1:
		break;
	case 1:
		break;
	default:
		fprintf(stderr, "invalid balance\n");
		return -EINVAL;
	}

	if (!__AVLH(has_child)(avl, node, AVL_LEFT))
		return 0;

	err = avl_check_visit(avl, __AVLH(left)(avl, node), level + 1);
	if (err < 0)
		return err;

	return 0;
}

int __AVL(check)(const struct __AVL_T(avl) * avl,
		 const struct __AVL_T(avl_searchops) * ops)
{
	struct __AVL_T (avlh) * holder = __AVL(gettop)(avl), *last;
	int err;

	if (!holder)
		return 0;

	err = avl_check_visit(avl, holder, 0);
	if (err < 0)
		return err;

	last = NULL;
	for (holder = __AVL(gethead)(avl); holder;
	     holder = __AVL(next)(avl, holder)) {
		if (last != NULL)
			if (ops->cmp(holder, last) < 0) {
				fprintf(stderr, "disordered nodes\n");
				return -EINVAL;
			}
		last = holder;
	}

	return 0;
}
