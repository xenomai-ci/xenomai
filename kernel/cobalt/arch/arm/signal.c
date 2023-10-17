// SPDX-License-Identifier: GPL-2.0

#include <cobalt/kernel/thread.h>

int xnarch_setup_trap_info(unsigned int vector, struct pt_regs *regs,
			   int *sig, struct kernel_siginfo *info)
{
	switch (vector) {
	case ARM_TRAP_ACCESS:
	case ARM_TRAP_SECTION:
	case ARM_TRAP_DABT:
	case ARM_TRAP_PABT:
		*sig = SIGSEGV;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = 0;
		break;
	case ARM_TRAP_FPU:
	case ARM_TRAP_VFP:
		*sig = SIGFPE;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = (void __user *)xnarch_fault_pc(regs);
		break;
	case ARM_TRAP_UNDEFINSTR:
		*sig = SIGILL;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_addr = (void __user *)xnarch_fault_pc(regs);
		break;
	case ARM_TRAP_ALIGNMENT:
		*sig = SIGBUS;
		info->si_errno = 0;
		info->si_code = BUS_ADRALN;
		info->si_addr = 0;
		break;
	default:
		return -ENOSYS;
	}

	info->si_signo = *sig;

	return 0;
}
