/*
 * Copyright (C) 2007,2011 Jan Kiszka <jan.kiszka@web.de>.
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

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <boilerplate/atomic.h>
#include <boilerplate/compiler.h>
#include <cobalt/tunables.h>
#include <cobalt/sys/cobalt.h>
#include "internal.h"

#define RT_PRINT_DEFAULT_BUFFER		16*1024
#define RT_PRINT_DEFAULT_SYNCDELAY	100 /* ms */
#define RT_PRINT_DEFAULT_BUFFERS_COUNT  4

#define RT_PRINT_LINE_BREAK		256

#define RT_PRINT_SYSLOG_STREAM		NULL

#define RT_PRINT_MODE_FORMAT		0
#define RT_PRINT_MODE_FWRITE		1

struct entry_head {
	FILE *dest;
	uint32_t seq_no;
	int priority;
	size_t len;
	char data[0];
} __attribute__((packed));

struct print_buffer {
	off_t write_pos;

	struct print_buffer *next, *prev;

	void *ring;
	size_t size;

	char name[32];

	/*
	 * Keep read_pos separated from write_pos to optimise write
	 * caching on SMP.
	 */
	off_t read_pos;
};

__weak int __cobalt_print_bufsz = RT_PRINT_DEFAULT_BUFFER;

int __cobalt_print_bufcount = RT_PRINT_DEFAULT_BUFFERS_COUNT;

int __cobalt_print_syncdelay = RT_PRINT_DEFAULT_SYNCDELAY;

static struct print_buffer *first_buffer;
static int buffers;
static uint32_t seq_no;
static struct timespec syncdelay;
static pthread_mutex_t buffer_lock;
static pthread_cond_t printer_wakeup;
static pthread_key_t buffer_key;
static pthread_key_t cleanup_key;
static pthread_t printer_thread;
static atomic_long_t *pool_bitmap;
static unsigned pool_bitmap_len;
static unsigned pool_buf_size;
static unsigned long pool_start, pool_len;

static void release_buffer(struct print_buffer *buffer);
static void print_buffers(void);

/* *** rt_print API *** */

static int 
vprint_to_buffer(FILE *stream, int fortify_level, int priority, 
		 unsigned int mode, size_t sz, const char *format, va_list args)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);
	off_t write_pos, read_pos;
	struct entry_head *head;
	int len, str_len;
	int res = 0;

	if (!buffer) {
		res = rt_print_init(0, NULL);
		if (res) {
			errno = res;
			return -1;
		}
		buffer = pthread_getspecific(buffer_key);
	}

	/* Take a snapshot of the ring buffer state */
	write_pos = buffer->write_pos;
	read_pos = buffer->read_pos;
	smp_mb();

	/* Is our write limit the end of the ring buffer? */
	if (write_pos >= read_pos) {
		/* Keep a safety margin to the end for at least an empty entry */
		len = buffer->size - write_pos - sizeof(struct entry_head);

		/* Special case: We were stuck at the end of the ring buffer
		   with space left there only for one empty entry. Now
		   read_pos was moved forward and we can wrap around. */
		if (len == 0 && read_pos > sizeof(struct entry_head)) {
			/* Write out empty entry */
			head = buffer->ring + write_pos;
			head->seq_no = seq_no;
			head->priority = 0;
			head->len = 0;

			/* Forward to the ring buffer start */
			write_pos = 0;
			len = read_pos - 1;
		}
	} else {
		/* Our limit is the read_pos ahead of our write_pos. One byte
		   margin is required to detect a full ring. */
		len = read_pos - write_pos - 1;
	}

	/* Account for head length */
	len -= sizeof(struct entry_head);
	if (len < 0)
		len = 0;

	head = buffer->ring + write_pos;

	if (mode == RT_PRINT_MODE_FORMAT) {
		if (stream != RT_PRINT_SYSLOG_STREAM) {
			/* We do not need the terminating \0 */
#ifdef CONFIG_XENO_FORTIFY
			if (fortify_level > 0)
				res = __vsnprintf_chk(head->data, len,
						      fortify_level - 1,
						      len > 0 ? len : 0, 
						      format, args);
			else
#else
				(void)fortify_level;
#endif
			res = vsnprintf(head->data, len, format, args);

			if (res < len) {
				/* Text was written completely, res contains its
				   length */
				len = res;
			} else {
				/* Text was truncated */
				res = len;
			}
		} else {
			/* We DO need the terminating \0 */
#ifdef CONFIG_XENO_FORTIFY
			if (fortify_level > 0)
				res = __vsnprintf_chk(head->data, len,
						      fortify_level - 1,
						      len > 0 ? len : 0, 
						      format, args);
			else
#endif
				res = vsnprintf(head->data, len, format, args);

			if (res < len) {
				/* Text was written completely, res contains its
				   length */
				len = res + 1;
			} else {
				/* Text was truncated */
				res = len;
			}
		}
	} else if (len >= 1) {
		str_len = sz;
		len = (str_len < len) ? str_len : len;
		memcpy(head->data, format, len);
	} else
		len = 0;

	/* If we were able to write some text, finalise the entry */
	if (len > 0) {
		head->seq_no = ++seq_no;
		head->priority = priority;
		head->dest = stream;
		head->len = len;

		/* Move forward by text and head length */
		write_pos += len + sizeof(struct entry_head);
	}

	/* Wrap around early if there is more space on the other side */
	if (write_pos >= buffer->size - RT_PRINT_LINE_BREAK &&
	    read_pos <= write_pos && read_pos > buffer->size - write_pos) {
		/* An empty entry marks the wrap-around */
		head = buffer->ring + write_pos;
		head->seq_no = seq_no;
		head->priority = priority;
		head->len = 0;

		write_pos = 0;
	}

	/* All entry data must be written before we can update write_pos */
	smp_wmb();

	buffer->write_pos = write_pos;

	return res;
}

static int print_to_buffer(FILE *stream, int priority, unsigned int mode,
			   size_t sz, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = vprint_to_buffer(stream, 0, priority, mode, sz, format, args);
	va_end(args);

	return ret;
}

int rt_vfprintf(FILE *stream, const char *format, va_list args)
{
	return vprint_to_buffer(stream, 0, 0,
				RT_PRINT_MODE_FORMAT, 0, format, args);
}

#ifdef CONFIG_XENO_FORTIFY

int __rt_vfprintf_chk(FILE *stream, int level, const char *fmt, va_list args)
{
	return vprint_to_buffer(stream, level + 1, 0,
				RT_PRINT_MODE_FORMAT, 0, fmt, args);
}

#endif

int rt_vprintf(const char *format, va_list args)
{
	return rt_vfprintf(stdout, format, args);
}

int rt_fprintf(FILE *stream, const char *format, ...)
{
	va_list args;
	int n;

	va_start(args, format);
	n = rt_vfprintf(stream, format, args);
	va_end(args);

	return n;
}

int rt_printf(const char *format, ...)
{
	va_list args;
	int n;

	va_start(args, format);
	n = rt_vfprintf(stdout, format, args);
	va_end(args);

	return n;
}

int rt_fputs(const char *s, FILE *stream)
{
	return print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, strlen(s), s);
}

int rt_puts(const char *s)
{
	int res;

	res = rt_fputs(s, stdout);
	if (res < 0)
		return res;

	return print_to_buffer(stdout, 0, RT_PRINT_MODE_FWRITE, 1, "\n");
}

int rt_fputc(int c, FILE *stream)
{
	unsigned char uc = c;
	int rc;

	rc = print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, 1, (char *)&uc);
	if (rc < 0)
		return EOF;

	return (int)uc;
}

int rt_putchar(int c)
{
	return rt_fputc(c, stdout);
}

size_t rt_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	print_to_buffer(stream, 0, RT_PRINT_MODE_FWRITE, size * nmemb, ptr);
	return nmemb;
}


void rt_syslog(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vprint_to_buffer(RT_PRINT_SYSLOG_STREAM, 0, priority,
			 RT_PRINT_MODE_FORMAT, 0, format, args);
	va_end(args);
}

void rt_vsyslog(int priority, const char *format, va_list args)
{
	vprint_to_buffer(RT_PRINT_SYSLOG_STREAM, 0, priority,
			 RT_PRINT_MODE_FORMAT, 0, format, args);
}

#ifdef CONFIG_XENO_FORTIFY

void __rt_vsyslog_chk(int priority, int level, const char *fmt, va_list args)
{
	vprint_to_buffer(RT_PRINT_SYSLOG_STREAM, level + 1, priority,
			 RT_PRINT_MODE_FORMAT, 0, fmt, args);
}

#endif

static void set_buffer_name(struct print_buffer *buffer, const char *name)
{
	int n;

	n = sprintf(buffer->name, "%08lx", (unsigned long)pthread_self());
	if (name) {
		buffer->name[n++] = ' ';
		strncpy(buffer->name+n, name, sizeof(buffer->name)-n-1);
		buffer->name[sizeof(buffer->name)-1] = 0;
	}
}

static void rt_print_init_inner(struct print_buffer *buffer, size_t size)
{
	buffer->size = size;

	memset(buffer->ring, 0, size);

	buffer->read_pos  = 0;
	buffer->write_pos = 0;

	buffer->prev = NULL;

	pthread_mutex_lock(&buffer_lock);

	buffer->next = first_buffer;
	if (first_buffer)
		first_buffer->prev = buffer;
	first_buffer = buffer;

	buffers++;
	pthread_cond_signal(&printer_wakeup);

	pthread_mutex_unlock(&buffer_lock);
}

int rt_print_init(size_t buffer_size, const char *buffer_name)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);
	size_t size = buffer_size;
	unsigned long old_bitmap;
	unsigned j;

	if (!size)
		size = __cobalt_print_bufsz;
	else if (size < RT_PRINT_LINE_BREAK)
		return EINVAL;

	if (buffer) {
		/* Only set name if buffer size is unchanged or default */
		if (size == buffer->size || !buffer_size) {
			set_buffer_name(buffer, buffer_name);
			return 0;
		}
		release_buffer(buffer);
		buffer = NULL;
	}

	/* Find a free buffer in the pool */
	do {
		unsigned long bitmap;
		unsigned i;

		for (i = 0; i < pool_bitmap_len; i++) {
			old_bitmap = atomic_long_read(&pool_bitmap[i]);
			if (old_bitmap)
				goto acquire;
		}

		goto not_found;

	  acquire:
		do {
			bitmap = old_bitmap;
			j = __builtin_ffsl(bitmap) - 1;
			old_bitmap = atomic_cmpxchg(&pool_bitmap[i],
						    bitmap,
						    bitmap & ~(1UL << j));
		} while (old_bitmap != bitmap && old_bitmap);
		j += i * LONG_BIT;
	} while (!old_bitmap);

	buffer = (struct print_buffer *)(pool_start + j * pool_buf_size);

  not_found:

	if (!buffer) {
		cobalt_assert_nrt();

		buffer = malloc(sizeof(*buffer));
		if (!buffer)
			return ENOMEM;

		buffer->ring = malloc(size);
		if (!buffer->ring)
			return ENOMEM;

		rt_print_init_inner(buffer, size);
	}

	set_buffer_name(buffer, buffer_name);

	pthread_setspecific(buffer_key, buffer);

	return 0;
}

const char *rt_print_buffer_name(void)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);
	int res;

	if (!buffer) {
		res = rt_print_init(0, NULL);
		if (res)
			return NULL;

		buffer = pthread_getspecific(buffer_key);
	}

	return buffer->name;
}

/* *** Deferred Output Management *** */
void rt_print_flush_buffers(void)
{
	int old_state;

	cobalt_thread_relax();

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);

	pthread_mutex_lock(&buffer_lock);
	print_buffers();
	pthread_mutex_unlock(&buffer_lock);

	pthread_setcancelstate(old_state, NULL);
}

static void release_buffer(struct print_buffer *buffer)
{
	struct print_buffer *prev, *next;
	unsigned long old_bitmap, bitmap;
	unsigned int i, j;

	cobalt_assert_nrt();

	pthread_setspecific(buffer_key, NULL);

	pthread_mutex_lock(&buffer_lock);

	print_buffers();

	pthread_mutex_unlock(&buffer_lock);

	/* Return the buffer to the pool */
	if ((unsigned long)buffer - pool_start >= pool_len)
		goto dofree;

	j = ((unsigned long)buffer - pool_start) / pool_buf_size;
	i = j / LONG_BIT;
	j = j % LONG_BIT;

	old_bitmap = atomic_long_read(&pool_bitmap[i]);
	do {
		bitmap = old_bitmap;
		old_bitmap = atomic_cmpxchg(&pool_bitmap[i],
					    bitmap,
					    bitmap | (1UL << j));
	} while (old_bitmap != bitmap);

	return;
dofree:
	pthread_mutex_lock(&buffer_lock);

	prev = buffer->prev;
	next = buffer->next;

	if (prev)
		prev->next = next;
	else
		first_buffer = next;
	if (next)
		next->prev = prev;

	buffers--;

	pthread_mutex_unlock(&buffer_lock);

	free(buffer->ring);
	free(buffer);
}

static void do_cleanup(void *arg)
{
	struct print_buffer *buffer = pthread_getspecific(buffer_key);

	if (buffer)
		release_buffer(buffer);

	pthread_cancel(printer_thread);
}

static inline uint32_t get_next_seq_no(struct print_buffer *buffer)
{
	struct entry_head *head = buffer->ring + buffer->read_pos;
	return head->seq_no;
}

static struct print_buffer *get_next_buffer(void)
{
	struct print_buffer *pos = first_buffer;
	struct print_buffer *buffer = NULL;
	uint32_t next_seq_no = 0; /* silence gcc... */

	while (pos) {
		if (pos->read_pos != pos->write_pos &&
		    (!buffer || get_next_seq_no(pos) < next_seq_no)) {
			buffer = pos;
			next_seq_no = get_next_seq_no(pos);
		}
		pos = pos->next;
	}

	return buffer;
}

static void print_buffers(void)
{
	struct print_buffer *buffer;
	struct entry_head *head;
	off_t read_pos;
	int len, ret;

	while (1) {
		buffer = get_next_buffer();
		if (!buffer)
			break;

		read_pos = buffer->read_pos;
		head = buffer->ring + read_pos;
		len = head->len;

		if (len) {
			/* Print out non-empty entry and proceed */
			/* Check if output goes to syslog */
			if (head->dest == RT_PRINT_SYSLOG_STREAM) {
				syslog(head->priority,
				       "%s", head->data);
			} else {
				ret = fwrite(head->data,
					     head->len, 1, head->dest);
				(void)ret;
			}

			read_pos += sizeof(*head) + len;
		} else {
			/* Emptry entries mark the wrap-around */
			read_pos = 0;
		}

		/* Make sure we have read the entry competely before
		   forwarding read_pos */
		smp_rmb();
		buffer->read_pos = read_pos;

		/* Enforce the read_pos update before proceeding */
		smp_wmb();
	}
}

static void *printer_loop(void *arg)
{
	while (1) {
		pthread_mutex_lock(&buffer_lock);

		while (buffers == 0)
			pthread_cond_wait(&printer_wakeup, &buffer_lock);

		print_buffers();

		pthread_mutex_unlock(&buffer_lock);

		nanosleep(&syncdelay, NULL);
	}

	return NULL;
}

static void spawn_printer_thread(void)
{
	pthread_attr_t thattr;
	sigset_t sset, oset;

	pthread_attr_init(&thattr);
	sigfillset(&sset);
	pthread_sigmask(SIG_BLOCK, &sset, &oset);
	pthread_create(&printer_thread, &thattr, printer_loop, NULL);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);
	pthread_setname_np(printer_thread, "cobalt_printf");
}

void cobalt_print_init_atfork(void)
{
	struct print_buffer *my_buffer = pthread_getspecific(buffer_key);
	struct print_buffer **pbuffer = &first_buffer;

	if (my_buffer) {
		/* Any content of my_buffer should be printed by our parent,
		   not us. */
		memset(my_buffer->ring, 0, my_buffer->size);

		my_buffer->read_pos  = 0;
		my_buffer->write_pos = 0;
	}

	/* re-init to avoid finding it locked by some parent thread */
	pthread_mutex_init(&buffer_lock, NULL);

	while (*pbuffer) {
		if (*pbuffer == my_buffer)
			pbuffer = &(*pbuffer)->next;
		else if ((unsigned long)*pbuffer - pool_start < pool_len) {
			release_buffer(*pbuffer);
			pbuffer = &(*pbuffer)->next;
		}
		else
			release_buffer(*pbuffer);
	}

	spawn_printer_thread();
}

void cobalt_print_init(void)
{
	unsigned int i;

	first_buffer = NULL;
	seq_no = 0;

	syncdelay.tv_sec  = __cobalt_print_syncdelay / 1000;
	syncdelay.tv_nsec = (__cobalt_print_syncdelay % 1000) * 1000000;

	/* Fill the buffer pool */
	pool_bitmap_len = (__cobalt_print_bufcount+LONG_BIT-1)/LONG_BIT;
	if (!pool_bitmap_len)
		goto done;

	pool_bitmap = malloc(pool_bitmap_len * sizeof(*pool_bitmap));
	if (!pool_bitmap)
		early_panic("error allocating print relay buffers");

	pool_buf_size = sizeof(struct print_buffer) + __cobalt_print_bufsz;
	pool_len = __cobalt_print_bufcount * pool_buf_size;
	pool_start = (unsigned long)malloc(pool_len);
	if (!pool_start)
		early_panic("error allocating print relay buffers");

	for (i = 0; i < __cobalt_print_bufcount / LONG_BIT; i++)
		atomic_long_set(&pool_bitmap[i], ~0UL);
	if (__cobalt_print_bufcount % LONG_BIT)
		atomic_long_set(&pool_bitmap[i],
				(1UL << (__cobalt_print_bufcount % LONG_BIT)) - 1);

	for (i = 0; i < __cobalt_print_bufcount; i++) {
		struct print_buffer *buffer =
			(struct print_buffer *)
			(pool_start + i * pool_buf_size);
		
		buffer->ring = (char *)(buffer + 1);

		rt_print_init_inner(buffer, __cobalt_print_bufsz);
	}
done:
	pthread_mutex_init(&buffer_lock, NULL);
	pthread_key_create(&buffer_key, (void (*)(void*))release_buffer);
	pthread_key_create(&cleanup_key, do_cleanup);
	pthread_cond_init(&printer_wakeup, NULL);
	spawn_printer_thread();
	/* We just need a non-zero TSD to trigger the dtor upon unwinding. */
	pthread_setspecific(cleanup_key, &cleanup_key);

	atexit(rt_print_flush_buffers);
}

COBALT_IMPL(int, vfprintf, (FILE *stream, const char *fmt, va_list args))
{
	if (!cobalt_is_relaxed())
		return rt_vfprintf(stream, fmt, args);
	else {
		rt_print_flush_buffers();
		return __STD(vfprintf(stream, fmt, args));
	}
}

COBALT_IMPL(int, vprintf, (const char *fmt, va_list args))
{
	return __COBALT(vfprintf(stdout, fmt, args));
}

COBALT_IMPL(int, fprintf, (FILE *stream, const char *fmt, ...))
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = __COBALT(vfprintf(stream, fmt, args));
	va_end(args);

	return rc;
}

COBALT_IMPL(int, printf, (const char *fmt, ...))
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = __COBALT(vfprintf(stdout, fmt, args));
	va_end(args);

	return rc;
}

COBALT_IMPL(int, fputs, (const char *s, FILE *stream))
{
	if (!cobalt_is_relaxed())
		return rt_fputs(s, stream);
	else {
		rt_print_flush_buffers();
		return __STD(fputs(s, stream));
	}
}

COBALT_IMPL(int, puts, (const char *s))
{
	if (!cobalt_is_relaxed())
		return rt_puts(s);
	else {
		rt_print_flush_buffers();
		return __STD(puts(s));
	}
}

COBALT_IMPL(int, fputc, (int c, FILE *stream))
{
	if (!cobalt_is_relaxed())
		return rt_fputc(c, stream);
	else {
		rt_print_flush_buffers();
		return __STD(fputc(c, stream));
	}
}

COBALT_IMPL(int, putchar, (int c))
{
	if (!cobalt_is_relaxed())
		return rt_putchar(c);
	else {
		rt_print_flush_buffers();
		return __STD(putchar(c));
	}
}

COBALT_IMPL(size_t, fwrite, (const void *ptr, size_t size, size_t nmemb, FILE *stream))
{
	if (!cobalt_is_relaxed())
		return rt_fwrite(ptr, size, nmemb, stream);
	else {
		rt_print_flush_buffers();
		return __STD(fwrite(ptr, size, nmemb, stream));
	}

}

COBALT_IMPL(int, fclose, (FILE *stream))
{
	rt_print_flush_buffers();
	return __STD(fclose(stream));
}

COBALT_IMPL(void, vsyslog, (int priority, const char *fmt, va_list ap))
{
	if (!cobalt_is_relaxed())
		return rt_vsyslog(priority, fmt, ap);
	else {
		rt_print_flush_buffers();
		__STD(vsyslog(priority, fmt, ap));
	}
}

COBALT_IMPL(void, syslog, (int priority, const char *fmt, ...))
{
	va_list args;

	va_start(args, fmt);
	__COBALT(vsyslog(priority, fmt, args));
	va_end(args);
}

/* 
 * Checked versions for -D_FORTIFY_SOURCE
 */
COBALT_IMPL(int, __vfprintf_chk, (FILE *f, int flag, const char *fmt, va_list ap))
{
#ifdef CONFIG_XENO_FORTIFY
	if (!cobalt_is_relaxed())
		return __rt_vfprintf_chk(f, flag, fmt, ap);
	else {
		rt_print_flush_buffers();
		return __STD(__vfprintf_chk(f, flag, fmt, ap));
	}
#else
	panic("--enable-fortify is required with applications enabling _FORTIFY_SOURCE");
#endif
}

COBALT_IMPL(int, __vprintf_chk, (int flag, const char *fmt, va_list ap))
{
	return __COBALT(__vfprintf_chk(stdout, flag, fmt, ap));
}

COBALT_IMPL(int, __fprintf_chk, (FILE *f, int flag, const char *fmt, ...))
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __COBALT(__vfprintf_chk(f, flag, fmt, args));
	va_end(args);

	return ret;
}

COBALT_IMPL(int, __printf_chk, (int flag, const char *fmt, ...))
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __COBALT(__vprintf_chk(flag, fmt, args));
	va_end(args);

	return ret;
}

COBALT_IMPL(void, __vsyslog_chk, (int pri, int flag, const char *fmt, va_list ap))
{
#ifdef CONFIG_XENO_FORTIFY
	if (!cobalt_is_relaxed())
		return __rt_vsyslog_chk(pri, flag, fmt, ap);
	else {
		rt_print_flush_buffers();
		__STD(__vsyslog_chk(pri, flag, fmt, ap));
	}
#else
	panic("--enable-fortify is required with applications enabling _FORTIFY_SOURCE");
#endif
}

COBALT_IMPL(void, __syslog_chk, (int pri, int flag, const char *fmt, ...))
{
	va_list args;

	va_start(args, fmt);
	__COBALT(__vsyslog_chk(pri, flag, fmt, args));
	va_end(args);
}
