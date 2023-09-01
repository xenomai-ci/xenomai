// SPDX-License-Identifier: GPL-2.0

#include <linux/signal.h>
#include <linux/uaccess.h>
#include <cobalt/kernel/thread.h>

#include <asm/sigframe.h>
#include <asm/sighandling.h>
#include <asm/fpu/signal.h>
#include <asm/trapnr.h>

int xnarch_setup_trap_info(unsigned int vector, struct pt_regs *regs,
			   int *sig, struct kernel_siginfo *info)
{
	switch (vector) {
	case X86_TRAP_DE: /* divide_error */
		*sig = SIGFPE;
		info->si_errno = 0;
		info->si_code = FPE_INTDIV;
		info->si_addr = (void __user *)regs->ip;
		break;
	case X86_TRAP_UD: /* invalid_op */
		*sig = SIGILL;
		info->si_errno = 0;
		info->si_code = ILL_ILLOPN;
		info->si_addr = (void __user *)regs->ip;
		break;
	case X86_TRAP_MF:
		*sig = SIGFPE;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = (void __user *)regs->ip;
		break;
	case X86_TRAP_PF:
		*sig = SIGSEGV;
		info->si_errno = xnarch_fault_code(regs);
		info->si_code = 0;
		info->si_addr = 0;
		break;
	default:
		return -ENOSYS;
	}

	info->si_signo = *sig;

	return 0;
}
