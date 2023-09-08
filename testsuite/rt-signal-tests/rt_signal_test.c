#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <copperplate/traceobj.h>
#include <alchemy/task.h>
#include <assert.h>
#include <setjmp.h>
#include <asm/ucontext.h>

#ifdef TEST_SEGFAULT
#define TEST_SIGNAL SIGSEGV
#elif defined(TEST_FPE)
#define TEST_SIGNAL SIGFPE
#else
#define TEST_SIGNAL SIGILL
#endif

#define BIT_FAULT_HANDLER_ENTERD     1
#define BIT_FAULT_HANDLER_FINISHED   2
#define BIT_FAULT_HANDLER_WRONG_TASK 4
#define BIT_FAULT_HANDLER_WRONG_SIG  8
#define BIT_DOMAIN_SWITCH            16

static RT_TASK t1;
static sig_atomic_t status = 0;
static sig_atomic_t cause = 0;

// #define JMP_CLEANUP
#ifdef JMP_CLEANUP
static jmp_buf jmpenv;
#endif

static int status_ok(void)
{
	return status ==
	       (BIT_FAULT_HANDLER_ENTERD | BIT_FAULT_HANDLER_FINISHED);
}

static const char *sigdebug_msg[] = {
	[SIGDEBUG_UNDEFINED] = "latency: received SIGXCPU for unknown reason",
	[SIGDEBUG_MIGRATE_SIGNAL] = "received signal",
	[SIGDEBUG_MIGRATE_SYSCALL] = "invoked syscall",
	[SIGDEBUG_MIGRATE_FAULT] = "triggered fault",
	[SIGDEBUG_MIGRATE_PRIOINV] = "affected by priority inversion",
	[SIGDEBUG_NOMLOCK] = "Xenomai: process memory not locked "
			     "(missing mlockall?)",
	[SIGDEBUG_WATCHDOG] = "Xenomai: watchdog triggered "
			      "(period too short?)",
};


#ifndef JMP_CLEANUP
static int get_step(void)
{
#ifdef TEST_SEGFAULT
#if defined(__i386__) || defined(__x86_64__)
	return 11;
#else
	return 4;
#endif
#elif defined(TEST_FPE)
	return 2;
#else
#if defined(__i386__) || defined(__x86_64__)
	return 2;
#else
	return 4;
#endif
#endif
}

static void do_cleanup(void *context)
{
	int step = 0;
	struct ucontext *uc = context;

	step = get_step();

#if defined(__i386__)
	uc->uc_mcontext.eip += step;
#elif defined(__x86_64__)
	uc->uc_mcontext.rip += step;
#elif defined(__aarch64__)
	uc->uc_mcontext.pc += step;
#endif
}
#endif

static void signal_handler(int sig, siginfo_t *info, void *context)
{
	RT_TASK *self;
	(void)context;

	if (sig == SIGDEBUG) {
		cause = sigdebug_reason(info);

		if (cause > SIGDEBUG_WATCHDOG)
			cause = SIGDEBUG_UNDEFINED;

		/* XXX: caused by rt_task_delete() */
		if (!(status & BIT_FAULT_HANDLER_ENTERD) &&
		    cause == SIGDEBUG_MIGRATE_SYSCALL) {
			return;
		}

		status |= BIT_DOMAIN_SWITCH;
	} else {
		status |= BIT_FAULT_HANDLER_ENTERD;
		if (sig != TEST_SIGNAL) {
			status |= BIT_FAULT_HANDLER_WRONG_SIG;
			return;
		}

		self = rt_task_self();

		if (self == NULL || !rt_task_same(self, &t1)) {
			status |= BIT_FAULT_HANDLER_WRONG_TASK;
		} else {
			status |= BIT_FAULT_HANDLER_FINISHED;
		}

#ifdef JMP_CLEANUP
		longjmp(jmpenv, 1);
#else
		do_cleanup(context);
#endif
	}
}

static void do_div_by_0(void)
{
#if defined(__i386__) || defined (__x86_64__)
	__asm__("xorl %eax, %eax\n\t"
		"movl %eax, %ecx\n\t"
		"cltd\n\t"
		"idivl %ecx");
#endif
	//TODO find a cortex-A way to trigger an FPE
}

static void do_ill(void)
{
#if defined(__i386__) || defined (__x86_64__)
	__asm__("ud2");
#else
	__asm__("udf #0xdead");
#endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static void do_segfault(void)
{
	*((int *)0x73) = 0xdecafbad;
}
#pragma GCC diagnostic pop

static void task_proc(void *arg)
{
	int ret;
	(void)arg;

	ret = cobalt_rt_signal(TEST_SIGNAL, signal_handler);
	assert(ret == 0);
	(void)ret;

#ifdef JMP_CLEANUP
	if (setjmp(jmpenv) != 0)
		return;
#endif

#ifdef TEST_SEGFAULT
	do_segfault();
#elif defined(TEST_FPE)
	do_div_by_0();
#else
	do_ill();
#endif

	if (0) { // so we don't geht defined but not used errors
		do_segfault();
		do_div_by_0();
		do_ill();
	}
}

static int test_sucessfull(void)
{
	if (status_ok()) {
		fputs("Test passed\n", stderr);
		return 1;
	}

	if (!(status & BIT_FAULT_HANDLER_ENTERD))
		fputs("Test failed: signal handler not invoked!\n", stderr);
	if ((status & BIT_FAULT_HANDLER_WRONG_TASK))
		fputs("Test failed: Signal handler in wrong task!\n", stderr);
	if ((status & BIT_FAULT_HANDLER_WRONG_SIG))
		fputs("Test failed: Signal handler of wrong signal!\n", stderr);
	if ((status & BIT_DOMAIN_SWITCH)) {
		fputs("Test failed: domain switch happened!\n", stderr);
		fprintf(stderr, "Caused by: %s\n", sigdebug_msg[cause]);
	}

	return 0;
}

int main(void)
{
	struct sigaction sa;
	int ret;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = signal_handler;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(TEST_SIGNAL, &sa, NULL);
	sigaction(SIGDEBUG, &sa, NULL);

	ret = rt_task_create(&t1, "rt-task", 0, 20, T_JOINABLE | T_WARNSW);
	assert(ret == 0);

	ret = rt_task_start(&t1, task_proc, NULL);
	assert(ret == 0);

	ret = rt_task_join(&t1);
	assert(ret == 0);
	(void)ret; // so the compiler does not complain about not checking ret

	return test_sucessfull() ? EXIT_SUCCESS : EXIT_FAILURE;
}
