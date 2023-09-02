#include <stdio.h>
#include <smokey/smokey.h>
#include <cobalt/arith.h>
#include "arith-noinline.h"

smokey_test_plugin(arith,
		   SMOKEY_NOARGS,
		   "Check helpers for fast arithmetics"
);

static volatile unsigned nsec_per_sec = 1000000000;
static volatile unsigned sample_freq = 33000000;
static volatile long long arg = 0x3ffffffffffffffULL;

#define bench(display, f)						\
	do {								\
		unsigned long long result;				\
		avg = rejected = 0;					\
		for (i = 0; i < 10000; i++) {				\
		  	ticks_t start, end;				\
			unsigned long delta;				\
									\
			start = clockobj_get_ns();			\
			result = (f);					\
			end = clockobj_get_ns();			\
			delta = end - start;				\
									\
			if (i == 0 || delta < (avg / i) * 4) {		\
				avg += delta;				\
			} else						\
				++rejected;				\
		}							\
		if (rejected < 10000) {					\
			avg = xnarch_llimd(avg, 10000, 10000 - rejected); \
			avg -= calib;		\
			smokey_trace("%s: 0x%016llx: %lld.%03llu ns,"	\
				    " rejected %d/10000",		\
				    display, result, avg / 10000,	\
				    ((avg >= 0 ? avg : -avg) % 10000) / 10, \
				    rejected);				\
		} else							\
			smokey_warning("%s: rejected 10000/10000", display); \
	} while (0)

static int run_arith(struct smokey_test *t, int argc, char *const argv[])
{
	unsigned int mul, shft, rejected;
	long long avg, calib = 0;
#ifdef XNARCH_HAVE_NODIV_LLIMD
	struct xnarch_u32frac frac;
#endif
	int i;

	/* Prepare. */
	xnarch_init_llmulshft(nsec_per_sec, sample_freq, &mul, &shft);
	smokey_trace("mul: 0x%08x, shft: %d", mul, shft);
#ifdef XNARCH_HAVE_NODIV_LLIMD
	xnarch_init_u32frac(&frac, nsec_per_sec, sample_freq);
	smokey_trace("integ: %d, frac: 0x%08llx", frac.integ, frac.frac);
#endif /* XNARCH_HAVE_NODIV_LLIMD */

	smokey_trace("\nsigned positive operation: 0x%016llx * %u / %d",
		arg, nsec_per_sec, sample_freq);
	bench("inline calibration", 0);
	calib = avg;
	bench("inlined llimd", xnarch_llimd(arg, nsec_per_sec, sample_freq));
	bench("inlined llmulshft", xnarch_llmulshft(arg, mul, shft));
#ifdef XNARCH_HAVE_NODIV_LLIMD
	bench("inlined nodiv_llimd",
	      xnarch_nodiv_llimd(arg, frac.frac, frac.integ));
#endif /* XNARCH_HAVE_NODIV_LLIMD */

	calib = 0;
	bench("out of line calibration", dummy());
	calib = avg;
	bench("out of line llimd",
	      do_llimd(arg, nsec_per_sec, sample_freq));
	bench("out of line llmulshft", do_llmulshft(arg, mul, shft));
#ifdef XNARCH_HAVE_NODIV_LLIMD
	bench("out of line nodiv_llimd",
	      do_nodiv_llimd(arg, frac.frac, frac.integ));
#endif /* XNARCH_HAVE_NODIV_LLIMD */


	smokey_trace("\nsigned negative operation: 0x%016llx * %u / %d",
		     -arg, nsec_per_sec, sample_freq);
	calib = 0;
	bench("inline calibration", 0);
	calib = avg;
	bench("inlined llimd", xnarch_llimd(-arg, nsec_per_sec, sample_freq));
	bench("inlined llmulshft", xnarch_llmulshft(-arg, mul, shft));
#ifdef XNARCH_HAVE_NODIV_LLIMD
	bench("inlined nodiv_llimd",
	      xnarch_nodiv_llimd(-arg, frac.frac, frac.integ));
#endif /* XNARCH_HAVE_NODIV_LLIMD */

	calib = 0;
	bench("out of line calibration", dummy());
	calib = avg;
	bench("out of line llimd",
	      do_llimd(-arg, nsec_per_sec, sample_freq));
	bench("out of line llmulshft", do_llmulshft(-arg, mul, shft));
#ifdef XNARCH_HAVE_NODIV_LLIMD
	bench("out of line nodiv_llimd",
	      do_nodiv_llimd(-arg, frac.frac, frac.integ));
#endif /* XNARCH_HAVE_NODIV_LLIMD */

#ifdef XNARCH_HAVE_NODIV_LLIMD
	smokey_trace("\nunsigned operation: 0x%016llx * %u / %d",
		     arg, nsec_per_sec, sample_freq);
	calib = 0;
	bench("inline calibration", 0);
	calib = avg;
	bench("inlined nodiv_ullimd",
	      xnarch_nodiv_ullimd(arg, frac.frac, frac.integ));

	calib = 0;
	bench("out of line calibration", dummy());
	calib = avg;
	bench("out of line nodiv_ullimd",
	      do_nodiv_ullimd(arg, frac.frac, frac.integ));
#endif /* XNARCH_HAVE_NODIV_LLIMD */
	return 0;
}
