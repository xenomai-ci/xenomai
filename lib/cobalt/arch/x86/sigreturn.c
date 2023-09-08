#include <cobalt/uapi/syscall.h>
#include "internal.h"

extern void cobalt_sigreturn (void) asm ("__cobalt_sigreturn") __attribute__ ((visibility ("hidden")));

#define TO_STR(x) #x

#ifdef __x86_64__
#define build_restorer(syscall_bit, syscall)                                   \
	asm(".text\n"                                                          \
	    "    .align 16\n"                                                  \
	    "__cobalt_sigreturn:\n"                                            \
	    "    movq $ " TO_STR(syscall_bit) ", %rax\n"                       \
	    "    orq $ " TO_STR(syscall) ", %rax\n"                            \
	    "    syscall")
#endif

#ifdef __i386__
#define build_restorer(syscall_bit, syscall)                                   \
	asm(".text\n"                                                          \
	    "    .align 16\n"                                                  \
	    "__cobalt_sigreturn:\n"                                            \
	    "    movl $ " TO_STR(syscall_bit) ", %eax\n"                       \
	    "    orl $ " TO_STR(syscall) ", %eax\n"                            \
	    "    int  $0x80")
#endif

/*
 * __COBALT_SYSCALL_BIT | sc_cobalt_sigreturn
 */
build_restorer(__COBALT_SYSCALL_BIT, sc_cobalt_sigreturn);

void *cobalt_get_restorer(void)
{
	return &cobalt_sigreturn;
}
