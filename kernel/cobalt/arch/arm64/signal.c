// SPDX-License-Identifier: GPL-2.0

#include <cobalt/kernel/thread.h>

int xnarch_setup_trap_info(unsigned int vector, struct pt_regs *regs,
			   int *sig, struct kernel_siginfo *info)
{
	switch (vector) {
	case ARM64_TRAP_ACCESS:
	case ARM64_TRAP_SME:
		*sig = SIGSEGV;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = 0;
		break;
	case ARM64_TRAP_FPE:
		*sig = SIGFPE;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = (void __user *)xnarch_fault_pc(regs);
		break;
	case ARM64_TRAP_UNDI:
	case ARM64_TRAP_BTI:
		*sig = SIGILL;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = (void __user *)xnarch_fault_pc(regs);
		break;
	case ARM64_TRAP_ALIGN:
		*sig = SIGBUS;
		info->si_errno = 0;
		info->si_code = BUS_ADRALN;
		info->si_addr = 0;
		break;
	case ARM64_TRAP_SEA:
		*sig = SIGBUS;
		info->si_errno = 0;
		info->si_code = BUS_OBJERR;
		info->si_addr = 0;
		break;
	default:
		return -ENOSYS;
	}

	info->si_signo = *sig;

	return 0;
}
