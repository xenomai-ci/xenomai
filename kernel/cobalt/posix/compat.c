/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/err.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <cobalt/kernel/compat.h>
#include <asm/xenomai/syscall.h>
#include <xenomai/posix/mqueue.h>

int sys32_get_timespec(struct timespec64 *ts,
		       const struct old_timespec32 __user *u_cts)
{
	struct old_timespec32 cts;

	if (u_cts == NULL || !access_ok(u_cts, sizeof(*u_cts)))
		return -EFAULT;

	if (__xn_get_user(cts.tv_sec, &u_cts->tv_sec) ||
		__xn_get_user(cts.tv_nsec, &u_cts->tv_nsec))
		return -EFAULT;

	ts->tv_sec = cts.tv_sec;
	ts->tv_nsec = cts.tv_nsec;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_timespec);

int sys32_put_timespec(struct old_timespec32 __user *u_cts,
		       const struct timespec64 *ts)
{
	struct old_timespec32 cts;

	if (u_cts == NULL || !access_ok(u_cts, sizeof(*u_cts)))
		return -EFAULT;

	cts.tv_sec = ts->tv_sec;
	cts.tv_nsec = ts->tv_nsec;

	if (__xn_put_user(cts.tv_sec, &u_cts->tv_sec) ||
	    __xn_put_user(cts.tv_nsec, &u_cts->tv_nsec))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_put_timespec);

int sys32_get_itimerspec(struct itimerspec64 *its,
			 const struct old_itimerspec32 __user *cits)
{
	int ret = sys32_get_timespec(&its->it_value, &cits->it_value);

	return ret ?: sys32_get_timespec(&its->it_interval, &cits->it_interval);
}
EXPORT_SYMBOL_GPL(sys32_get_itimerspec);

int sys32_put_itimerspec(struct old_itimerspec32 __user *cits,
			 const struct itimerspec64 *its)
{
	int ret = sys32_put_timespec(&cits->it_value, &its->it_value);

	return ret ?: sys32_put_timespec(&cits->it_interval, &its->it_interval);
}
EXPORT_SYMBOL_GPL(sys32_put_itimerspec);

int sys32_get_timeval(struct __kernel_old_timeval *tv,
		      const struct old_timeval32 __user *ctv)
{
	return (ctv == NULL ||
		!access_ok(ctv, sizeof(*ctv)) ||
		__xn_get_user(tv->tv_sec, &ctv->tv_sec) ||
		__xn_get_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(sys32_get_timeval);

int sys32_put_timeval(struct old_timeval32 __user *ctv,
		      const struct __kernel_old_timeval *tv)
{
	return (ctv == NULL ||
		!access_ok(ctv, sizeof(*ctv)) ||
		__xn_put_user(tv->tv_sec, &ctv->tv_sec) ||
		__xn_put_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}
EXPORT_SYMBOL_GPL(sys32_put_timeval);

int sys32_get_timex(struct __kernel_timex *tx,
		    const struct old_timex32 __user *ctx)
{
	struct __kernel_old_timeval time;
	int ret;

	memset(tx, 0, sizeof(*tx));

	ret = sys32_get_timeval(&time, &ctx->time);
	if (ret)
		return ret;

	tx->time.tv_sec = time.tv_sec;
	tx->time.tv_usec = time.tv_usec;

	if (!access_ok(ctx, sizeof(*ctx)) ||
	    __xn_get_user(tx->modes, &ctx->modes) ||
	    __xn_get_user(tx->offset, &ctx->offset) ||
	    __xn_get_user(tx->freq, &ctx->freq) ||
	    __xn_get_user(tx->maxerror, &ctx->maxerror) ||
	    __xn_get_user(tx->esterror, &ctx->esterror) ||
	    __xn_get_user(tx->status, &ctx->status) ||
	    __xn_get_user(tx->constant, &ctx->constant) ||
	    __xn_get_user(tx->precision, &ctx->precision) ||
	    __xn_get_user(tx->tolerance, &ctx->tolerance) ||
	    __xn_get_user(tx->tick, &ctx->tick) ||
	    __xn_get_user(tx->ppsfreq, &ctx->ppsfreq) ||
	    __xn_get_user(tx->jitter, &ctx->jitter) ||
	    __xn_get_user(tx->shift, &ctx->shift) ||
	    __xn_get_user(tx->stabil, &ctx->stabil) ||
	    __xn_get_user(tx->jitcnt, &ctx->jitcnt) ||
	    __xn_get_user(tx->calcnt, &ctx->calcnt) ||
	    __xn_get_user(tx->errcnt, &ctx->errcnt) ||
	    __xn_get_user(tx->stbcnt, &ctx->stbcnt))
	  return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_timex);

int sys32_put_timex(struct old_timex32 __user *ctx,
		    const struct __kernel_timex *tx)
{
	struct __kernel_old_timeval time;
	int ret;

	time.tv_sec = tx->time.tv_sec;
	time.tv_usec = tx->time.tv_usec;

	ret = sys32_put_timeval(&ctx->time, &time);
	if (ret)
		return ret;

	if (!access_ok(ctx, sizeof(*ctx)) ||
	    __xn_put_user(tx->modes, &ctx->modes) ||
	    __xn_put_user(tx->offset, &ctx->offset) ||
	    __xn_put_user(tx->freq, &ctx->freq) ||
	    __xn_put_user(tx->maxerror, &ctx->maxerror) ||
	    __xn_put_user(tx->esterror, &ctx->esterror) ||
	    __xn_put_user(tx->status, &ctx->status) ||
	    __xn_put_user(tx->constant, &ctx->constant) ||
	    __xn_put_user(tx->precision, &ctx->precision) ||
	    __xn_put_user(tx->tolerance, &ctx->tolerance) ||
	    __xn_put_user(tx->tick, &ctx->tick) ||
	    __xn_put_user(tx->ppsfreq, &ctx->ppsfreq) ||
	    __xn_put_user(tx->jitter, &ctx->jitter) ||
	    __xn_put_user(tx->shift, &ctx->shift) ||
	    __xn_put_user(tx->stabil, &ctx->stabil) ||
	    __xn_put_user(tx->jitcnt, &ctx->jitcnt) ||
	    __xn_put_user(tx->calcnt, &ctx->calcnt) ||
	    __xn_put_user(tx->errcnt, &ctx->errcnt) ||
	    __xn_put_user(tx->stbcnt, &ctx->stbcnt))
	  return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_put_timex);

int sys32_get_fdset(fd_set *fds, const compat_fd_set __user *cfds,
		    size_t cfdsize)
{
	int rdpos, wrpos, rdlim = cfdsize / sizeof(compat_ulong_t);

	if (cfds == NULL || !access_ok(cfds, cfdsize))
		return -EFAULT;

	for (rdpos = 0, wrpos = 0; rdpos < rdlim; rdpos++, wrpos++)
		if (__xn_get_user(fds->fds_bits[wrpos], cfds->fds_bits + rdpos))
			return -EFAULT;

	return 0;
}

int sys32_put_fdset(compat_fd_set __user *cfds, const fd_set *fds,
		    size_t fdsize)
{
	int rdpos, wrpos, wrlim = fdsize / sizeof(long);

	if (cfds == NULL || !access_ok(cfds, wrlim * sizeof(compat_ulong_t)))
		return -EFAULT;

	for (rdpos = 0, wrpos = 0; wrpos < wrlim; rdpos++, wrpos++)
		if (__xn_put_user(fds->fds_bits[rdpos], cfds->fds_bits + wrpos))
			return -EFAULT;

	return 0;
}

int sys32_get_mqattr(struct mq_attr *ap,
		     const struct compat_mq_attr __user *u_cap)
{
	struct compat_mq_attr cattr;

	if (u_cap == NULL ||
	    cobalt_copy_from_user(&cattr, u_cap, sizeof(cattr)))
		return -EFAULT;

	ap->mq_flags = cattr.mq_flags;
	ap->mq_maxmsg = cattr.mq_maxmsg;
	ap->mq_msgsize = cattr.mq_msgsize;
	ap->mq_curmsgs = cattr.mq_curmsgs;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_mqattr);

int sys32_put_mqattr(struct compat_mq_attr __user *u_cap,
		     const struct mq_attr *ap)
{
	struct compat_mq_attr cattr;

	cattr.mq_flags = ap->mq_flags;
	cattr.mq_maxmsg = ap->mq_maxmsg;
	cattr.mq_msgsize = ap->mq_msgsize;
	cattr.mq_curmsgs = ap->mq_curmsgs;

	return u_cap == NULL ? -EFAULT :
		cobalt_copy_to_user(u_cap, &cattr, sizeof(cattr));
}
EXPORT_SYMBOL_GPL(sys32_put_mqattr);

int sys32_get_sigevent(struct sigevent *ev,
		       const struct compat_sigevent *__user u_cev)
{
	struct compat_sigevent cev;
	compat_int_t *cp;
	int ret, *p;

	if (u_cev == NULL)
		return -EFAULT;

	ret = cobalt_copy_from_user(&cev, u_cev, sizeof(cev));
	if (ret)
		return ret;

	memset(ev, 0, sizeof(*ev));
	ev->sigev_value.sival_ptr = compat_ptr(cev.sigev_value.sival_ptr);
	ev->sigev_signo = cev.sigev_signo;
	ev->sigev_notify = cev.sigev_notify;
	/*
	 * Extensions may define extra fields we don't know about in
	 * the padding area, so we have to load it entirely.
	 */
	p = ev->_sigev_un._pad;
	cp = cev._sigev_un._pad;
	while (p < &ev->_sigev_un._pad[ARRAY_SIZE(ev->_sigev_un._pad)] &&
	       cp < &cev._sigev_un._pad[ARRAY_SIZE(cev._sigev_un._pad)])
		*p++ = *cp++;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_sigevent);

int sys32_get_sigset(sigset_t *set, const compat_sigset_t *u_cset)
{
#ifdef __BIG_ENDIAN
	compat_sigset_t v;

	if (cobalt_copy_from_user(&v, u_cset, sizeof(compat_sigset_t)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: set->sig[3] = v.sig[6] | (((long)v.sig[7]) << 32 );
	case 3: set->sig[2] = v.sig[4] | (((long)v.sig[5]) << 32 );
	case 2: set->sig[1] = v.sig[2] | (((long)v.sig[3]) << 32 );
	case 1: set->sig[0] = v.sig[0] | (((long)v.sig[1]) << 32 );
	}
#else
	if (cobalt_copy_from_user(set, u_cset, sizeof(compat_sigset_t)))
		return -EFAULT;
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_sigset);

int sys32_put_sigset(compat_sigset_t *u_cset, const sigset_t *set)
{
#ifdef __BIG_ENDIAN
	compat_sigset_t v;
	switch (_NSIG_WORDS) {
	case 4: v.sig[7] = (set->sig[3] >> 32); v.sig[6] = set->sig[3];
	case 3: v.sig[5] = (set->sig[2] >> 32); v.sig[4] = set->sig[2];
	case 2: v.sig[3] = (set->sig[1] >> 32); v.sig[2] = set->sig[1];
	case 1: v.sig[1] = (set->sig[0] >> 32); v.sig[0] = set->sig[0];
	}
	return cobalt_copy_to_user(u_cset, &v, sizeof(*u_cset)) ? -EFAULT : 0;
#else
	return cobalt_copy_to_user(u_cset, set, sizeof(*u_cset)) ? -EFAULT : 0;
#endif
}
EXPORT_SYMBOL_GPL(sys32_put_sigset);

int sys32_get_sigval(union sigval *val, const union compat_sigval *u_cval)
{
	union compat_sigval cval;
	int ret;

	if (u_cval == NULL)
		return -EFAULT;

	ret = cobalt_copy_from_user(&cval, u_cval, sizeof(cval));
	if (ret)
		return ret;

	val->sival_ptr = compat_ptr(cval.sival_ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_sigval);

int sys32_put_siginfo(void __user *u_si, const struct siginfo *si,
		      int overrun)
{
	struct compat_siginfo __user *u_p = u_si;
	int ret;

	if (u_p == NULL)
		return -EFAULT;

	ret = __xn_put_user(si->si_signo, &u_p->si_signo);
	ret |= __xn_put_user(si->si_errno, &u_p->si_errno);
	ret |= __xn_put_user(si->si_code, &u_p->si_code);

	/*
	 * Copy the generic/standard siginfo bits to userland.
	 */
	switch (si->si_code) {
	case SI_TIMER:
		ret |= __xn_put_user(si->si_tid, &u_p->si_tid);
		ret |= __xn_put_user(ptr_to_compat(si->si_ptr), &u_p->si_ptr);
		ret |= __xn_put_user(overrun, &u_p->si_overrun);
		break;
	case SI_QUEUE:
	case SI_MESGQ:
		ret |= __xn_put_user(ptr_to_compat(si->si_ptr), &u_p->si_ptr);
		fallthrough;
	case SI_USER:
		ret |= __xn_put_user(si->si_pid, &u_p->si_pid);
		ret |= __xn_put_user(si->si_uid, &u_p->si_uid);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sys32_put_siginfo);

int sys32_get_msghdr(struct user_msghdr *msg,
		     const struct compat_msghdr __user *u_cmsg)
{
	compat_uptr_t tmp1, tmp2, tmp3;

	if (u_cmsg == NULL ||
	    !access_ok(u_cmsg, sizeof(*u_cmsg)) ||
	    __xn_get_user(tmp1, &u_cmsg->msg_name) ||
	    __xn_get_user(msg->msg_namelen, &u_cmsg->msg_namelen) ||
	    __xn_get_user(tmp2, &u_cmsg->msg_iov) ||
	    __xn_get_user(msg->msg_iovlen, &u_cmsg->msg_iovlen) ||
	    __xn_get_user(tmp3, &u_cmsg->msg_control) ||
	    __xn_get_user(msg->msg_controllen, &u_cmsg->msg_controllen) ||
	    __xn_get_user(msg->msg_flags, &u_cmsg->msg_flags))
		return -EFAULT;

	if (msg->msg_namelen > sizeof(struct sockaddr_storage))
		msg->msg_namelen = sizeof(struct sockaddr_storage);

	msg->msg_name = compat_ptr(tmp1);
	msg->msg_iov = compat_ptr(tmp2);
	msg->msg_control = compat_ptr(tmp3);

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_msghdr);

int sys32_get_mmsghdr(struct mmsghdr *mmsg,
		      const struct compat_mmsghdr __user *u_cmmsg)
{
	if (u_cmmsg == NULL ||
	    !access_ok(u_cmmsg, sizeof(*u_cmmsg)) ||
	    __xn_get_user(mmsg->msg_len, &u_cmmsg->msg_len))
		return -EFAULT;

	return sys32_get_msghdr(&mmsg->msg_hdr, &u_cmmsg->msg_hdr);
}
EXPORT_SYMBOL_GPL(sys32_get_mmsghdr);

int sys32_put_msghdr(struct compat_msghdr __user *u_cmsg,
		     const struct user_msghdr *msg)
{
	if (u_cmsg == NULL ||
	    !access_ok(u_cmsg, sizeof(*u_cmsg)) ||
	    __xn_put_user(ptr_to_compat(msg->msg_name), &u_cmsg->msg_name) ||
	    __xn_put_user(msg->msg_namelen, &u_cmsg->msg_namelen) ||
	    __xn_put_user(ptr_to_compat(msg->msg_iov), &u_cmsg->msg_iov) ||
	    __xn_put_user(msg->msg_iovlen, &u_cmsg->msg_iovlen) ||
	    __xn_put_user(ptr_to_compat(msg->msg_control), &u_cmsg->msg_control) ||
	    __xn_put_user(msg->msg_controllen, &u_cmsg->msg_controllen) ||
	    __xn_put_user(msg->msg_flags, &u_cmsg->msg_flags))
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_put_msghdr);

int sys32_put_mmsghdr(struct compat_mmsghdr __user *u_cmmsg,
		     const struct mmsghdr *mmsg)
{
	if (u_cmmsg == NULL ||
	    !access_ok(u_cmmsg, sizeof(*u_cmmsg)) ||
	    __xn_put_user(mmsg->msg_len, &u_cmmsg->msg_len))
		return -EFAULT;

	return sys32_put_msghdr(&u_cmmsg->msg_hdr, &mmsg->msg_hdr);
}
EXPORT_SYMBOL_GPL(sys32_put_mmsghdr);

int sys32_get_iovec(struct iovec *iov,
		    const struct compat_iovec __user *u_ciov,
		    int ciovlen)
{
	const struct compat_iovec __user *p;
	struct compat_iovec ciov;
	int ret, n;

	for (n = 0, p = u_ciov; n < ciovlen; n++, p++) {
		ret = cobalt_copy_from_user(&ciov, p, sizeof(ciov));
		if (ret)
			return ret;
		iov[n].iov_base = compat_ptr(ciov.iov_base);
		iov[n].iov_len = ciov.iov_len;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_get_iovec);

int sys32_put_iovec(struct compat_iovec __user *u_ciov,
		    const struct iovec *iov,
		    int iovlen)
{
	struct compat_iovec __user *p;
	struct compat_iovec ciov;
	int ret, n;

	for (n = 0, p = u_ciov; n < iovlen; n++, p++) {
		ciov.iov_base = ptr_to_compat(iov[n].iov_base);
		ciov.iov_len = iov[n].iov_len;
		ret = cobalt_copy_to_user(p, &ciov, sizeof(*p));
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sys32_put_iovec);
