dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.62)

# The config/version-code file defines the general versioning data
# as: <major>.<minor>.<subrev>, giving the full Xenomai version stamp.
# config/apirev defines the revision level of the user API we
# implement (which actually expresses the revision level of the
# Copperplate library).  The kernel ABI is Cobalt-specific and is
# defined for each architecture in the asm/features.h file.
AC_INIT([Xenomai],m4_normalize(m4_include([config/version-label])),xenomai@xenomai.org)

AC_CONFIG_HEADERS(include/xeno_config.h)
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR([config])
AC_CONFIG_SRCDIR(lib/cobalt/thread.c)
AC_PREFIX_DEFAULT(/usr/xenomai)
# We want $prefix to be set for the configure script
if test x$prefix = xNONE; then
   prefix=$ac_default_prefix
fi

version_code=`cat $srcdir/config/version-code`
CONFIG_XENO_VERSION_MAJOR=`expr $version_code : '\([[0-9]]*\)'`
CONFIG_XENO_VERSION_MINOR=`expr $version_code : '[[0-9]]*\.\([[0-9]]*\)'`
CONFIG_XENO_REVISION_LEVEL=`expr $version_code : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
CONFIG_XENO_UAPI_LEVEL=`cat $srcdir/config/apirev`
CONFIG_XENO_VERSION_STRING="$PACKAGE_VERSION"
topdir=`cd $srcdir && pwd`

dnl Find out whether we build for Cobalt core, i.e. dual kernel mode,
dnl or Mercury (single image kernel). Defaults to Cobalt.
rtcore_type=cobalt
AC_MSG_CHECKING([whether we build for Cobalt or Mercury core])
AC_ARG_WITH(core,
    AS_HELP_STRING([--with-core=<cobalt | mercury>],[build for dual kernel or single image]),
    [
	case "$withval" in
	"" | y | ye | yes | n | no)
	    AC_MSG_ERROR([You must supply an argument to --with-core])
	  ;;
	cobalt|mercury)
	   rtcore_type=$withval
	   ;;
	*)
	    AC_MSG_ERROR([--with-core=<cobalt | mercury>])
	esac
    ])
AC_MSG_RESULT($rtcore_type)

AM_CONDITIONAL(XENO_COBALT,[test x$rtcore_type = xcobalt])
test x$rtcore_type = xcobalt && AC_DEFINE(CONFIG_XENO_COBALT,1,[config])
AM_CONDITIONAL(XENO_MERCURY,[test x$rtcore_type = xmercury])
test x$rtcore_type = xmercury && AC_DEFINE(CONFIG_XENO_MERCURY,1,[config])
XENO_TARGET_CORE=$rtcore_type

if test "x$CFLAGS" = "x"; then
	XENO_EMPTY_CFLAGS=true
else
	XENO_EMPTY_CFLAGS=false
fi

if eval test $includedir = /usr/include; then
  AC_MSG_ERROR([Using /usr/include as includedir is not supported. Please change your --prefix or specify another --includedir])
fi

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_PROG_INSTALL

AC_ARG_WITH(cc,
    AS_HELP_STRING([--with-cc=compiler],[use specific C compiler]),
    [
	case "$withval" in
	"" | y | ye | yes | n | no)
	    AC_MSG_ERROR([You must supply an argument to --with-cc])
	  ;;
	esac
	CC="$withval"
    ])
AC_PROG_CC

# Do not let autoconf set the default value of CFLAGS
if $XENO_EMPTY_CFLAGS; then
	CFLAGS=""
fi

AC_PROG_CC_FOR_BUILD
AC_PROG_GREP
LT_PATH_NM

XENO_SYMBOL_PREFIX=
LT_SYS_SYMBOL_USCORE
if test \! x$sys_symbol_underscore = xno; then
   XENO_SYMBOL_PREFIX=_
fi
AC_SUBST(XENO_SYMBOL_PREFIX)

AC_DEFINE_UNQUOTED(CONFIG_XENO_BUILD_STRING,"$build",[Build system alias])
XENO_BUILD_STRING="$build"
AC_DEFINE_UNQUOTED(CONFIG_XENO_HOST_STRING,"$host",[Host system alias])
XENO_HOST_STRING="$host"
XENO_BUILD_COMPILER="`$CC -v 2>&1 | tail -n 1`"
AC_DEFINE_UNQUOTED(CONFIG_XENO_COMPILER,"$XENO_BUILD_COMPILER",[Compiler])

AM_INIT_AUTOMAKE([foreign no-exeext dist-bzip2 tar-ustar subdir-objects])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE
AM_PROG_AS
AM_PROG_LEX

XENO_BUILD_ARGS="$ac_configure_args"

AC_MSG_CHECKING([for target architecture])

if test x$host_alias = x; then
  build_for=$host
else
  build_for=$host_alias
fi

use_tls=no
case "$build_for" in
 i*86*-*)
	use_tls=yes
	target_cpu_arch=x86
	CONFIG_XENO_DEFAULT_PERIOD=100000
	;;
 ppc-*|powerpc-*)
	use_tls=yes
	target_cpu_arch=powerpc
	CONFIG_XENO_DEFAULT_PERIOD=100000
	;;
 arm*-*)
	target_cpu_arch=arm
	CONFIG_XENO_DEFAULT_PERIOD=1000000
	;;
 aarch64-*)
	target_cpu_arch=arm64
	CONFIG_XENO_DEFAULT_PERIOD=1000000
	;;
 x86_64-*|amd64-*)
	use_tls=yes
	target_cpu_arch=x86
	CONFIG_XENO_DEFAULT_PERIOD=100000
	;;
 *)
	if test $rtcore_type = cobalt; then
	   echo ""
	   echo "**********************************************"
	   echo "Cobalt not supported over $build_for."
	   echo "**********************************************"
	   echo ""
	   exit 1
	else
	   CONFIG_XENO_DEFAULT_PERIOD=100000
	   target_cpu_arch=`echo $build_for|cut -d- -f1`
	fi
	;;
esac

AC_MSG_RESULT([$target_cpu_arch])
XENO_TARGET_ARCH=$target_cpu_arch
AC_ENABLE_SHARED
AC_PROG_LIBTOOL

dnl
dnl Parse options
dnl

dnl Debug build (default: off, no symbols)

debug_mode=
debug_symbols=
AC_MSG_CHECKING(whether to enable debug mode)
AC_ARG_ENABLE(debug,
	AS_HELP_STRING([--enable-debug], [Enable debug mode in programs]),
	[case "$enableval" in
	symbols)
		debug_symbols=y
		;;
	y | yes | partial)
		debug_mode=partial
		debug_symbols=y
		;;
	full)
		debug_mode=full
		debug_symbols=y
		;;
	n | no)
		debug_mode=
		debug_symbols=
		;;
	*)
		 AC_MSG_ERROR([invalid debug level $enableval])
		 ;;
	esac])
AC_MSG_RESULT(${debug_mode:-no})
AM_CONDITIONAL(XENO_DEBUG,[test \! x$debug_mode = x])
test \! x$debug_mode = x && AC_DEFINE(CONFIG_XENO_DEBUG,1,[config])
AM_CONDITIONAL(XENO_DEBUG_FULL,[test x$debug_mode = xfull])
test x$debug_mode = xfull && AC_DEFINE(CONFIG_XENO_DEBUG_FULL,1,[config])

dnl Demo (default: on)

AC_ARG_ENABLE(demo,
	AS_HELP_STRING([--disable-demo], [Disable demonstration code]))
AM_CONDITIONAL(XENO_ENABLE_DEMO,[test x$enable_demo != xno])

dnl Testsuite (default: on)

AC_ARG_ENABLE(testsuite,
	AS_HELP_STRING([--disable-testsuite], [Disable testsuite]))
AM_CONDITIONAL(XENO_ENABLE_TESTSUITE,[test x$enable_testsuite != xno])

dnl Low resolution clock (default: off)

unset lores_clock
AC_MSG_CHECKING(whether to enable the low resolution clock)
AC_ARG_ENABLE(lores-clock,
	AS_HELP_STRING([--enable-lores-clock], [Enable low resolution clock]),
	[case "$enableval" in
	y | yes) lores_clock=y ;;
	*) unset lores_clock ;;
	esac])
AC_MSG_RESULT(${lores_clock:-no})
if test x$lores_clock = x; then
	AC_DEFINE(CONFIG_XENO_LORES_CLOCK_DISABLED,1,[config])
fi

dnl Raw monotonic clock (default: cobalt=on, mercury=off)

if test $rtcore_type = cobalt; then
   raw_monotonic_clock=y
else
   raw_monotonic_clock=
fi
AC_MSG_CHECKING(whether we may use CLOCK_MONOTONIC_RAW)
AC_ARG_ENABLE(clock-monotonic-raw,
	AS_HELP_STRING([--enable-clock-monotonic-raw], [Use CLOCK_MONOTONIC_RAW for timings]),
	[case "$enableval" in
	y | yes) raw_monotonic_clock=y ;;
	*) unset raw_monotonic_clock ;;
	esac])
AC_MSG_RESULT(${raw_monotonic_clock:-no})
if test x$raw_monotonic_clock = xy; then
	AC_DEFINE(CONFIG_XENO_RAW_CLOCK_ENABLED,1,[config])
fi

checkflags="-nostdinc -isystem \$(SYSROOT)/usr/include -Wbitwise -Wno-transparent-union -D_GNU_SOURCE -D_XOPEN_SOURCE=500 -D_REENTRANT \$(DEFS) \$(DEFAULT_INCLUDES) \$(INCLUDES) \$(AM_CPPFLAGS) \$(CPPFLAGS) -I\$(top_srcdir)/include -isystem \$(shell \$(CC) -print-file-name=include) -include \$(top_builddir)/include/xeno_config.h \$(shell \$(CC) -dM -E -xc /dev/null|sed -e 's/^\\#define /-D/' -e \"s/ /=\'/\" -e \"s/\$\$/\'/\")"

dnl Used with sparse
AC_SUBST(CHECKFLAGS, $checkflags)

dnl Enable assertions (default: depends on debug mode)

test x$debug_mode = x || use_assert=y
AC_MSG_CHECKING(whether assertions should be enabled)
AC_ARG_ENABLE(assert,
	AS_HELP_STRING([--enable-assert], [Enable runtime assertions]),
	[case "$enableval" in
	y | yes) use_assert=y ;;
	*) unset use_assert ;;
	esac])
AC_MSG_RESULT(${use_assert:-no})

dnl Enable asynchronous cancellation (default: off)

async_cancel=
AC_MSG_CHECKING(whether asynchronous cancellation of threads is enabled)
AC_ARG_ENABLE(async-cancel,
	AS_HELP_STRING([--enable-async-cancel], [Enable asynchronous cancellation]),
	[case "$enableval" in
	y | yes) async_cancel=y ;;
	n | no) unset async_cancel ;;
	esac])
AC_MSG_RESULT(${async_cancel:-no})

if test x$async_cancel = xy; then
	AC_DEFINE(CONFIG_XENO_ASYNC_CANCEL,1,[config])
fi

dnl Work-around for broken PI with condvars on Mercury (default: off)

unset workaround_condvar_pi
AC_MSG_CHECKING(whether to enable the workaround for broken PI with condvars)
AC_ARG_ENABLE(condvar-workaround,
	AS_HELP_STRING([--enable-condvar-workaround], [Enable workaround for broken PI with condvars in glibc]),
	[case "$enableval" in
	y | yes) workaround_condvar_pi=y ;;
	*) unset workaround_condvar_pi ;;
	esac])
AC_MSG_RESULT(${workaround_condvar_pi:-no})
if test x$workaround_condvar_pi = xy; then
   if test $rtcore_type = mercury; then
	AC_DEFINE(CONFIG_XENO_WORKAROUND_CONDVAR_PI,1,[config])
   else
        AC_MSG_WARN([PI workaround for condvars useless over Cobalt - ignoring])
   fi
fi

dnl Lazy schedparam propagation for Cobalt (default: off)

unset lazy_setsched_update
AC_MSG_CHECKING(whether to enable lazy scheduling parameter update)
AC_ARG_ENABLE(lazy-setsched,
	AS_HELP_STRING([--enable-lazy-setsched], [Enable lazy scheduling parameter update]),
	[case "$enableval" in
	y | yes) lazy_setsched_update=y ;;
	*) unset lazy_setsched_update ;;
	esac])
AC_MSG_RESULT(${lazy_setsched_update:-no})
if test x$lazy_setsched_update = xy; then
   if test x$rtcore_type = xcobalt; then
	AC_DEFINE(CONFIG_XENO_LAZY_SETSCHED,1,[config])
   else
        AC_MSG_WARN([No lazy scheduling parameter updates over Mercury - ignoring])
   fi
fi

dnl Enable shared multi-processing (default: off)

use_pshared=
AC_MSG_CHECKING(whether shared multi-processing should be supported)
AC_ARG_ENABLE(pshared,
	AS_HELP_STRING([--enable-pshared], [Enable shared multi-processing for capable skins]),
	[case "$enableval" in
	y | yes) use_pshared=y ;;
	*) unset use_pshared ;;
	esac])
AC_MSG_RESULT(${use_pshared:-no})

if test x$use_pshared = xy; then
	AC_DEFINE(CONFIG_XENO_PSHARED,1,[config])
fi
AM_CONDITIONAL(XENO_PSHARED,[test x$use_pshared = xy])

dnl Allocator selection

localmem_allocator=heapmem
AC_MSG_CHECKING([for process-local memory allocator])
AC_ARG_WITH(localmem,
    AS_HELP_STRING([--with-localmem=<heapmem | tlsf>],[Select process-local memory allocator]),
    [
	case "$withval" in
	"" | y | ye | yes | n | no)
	    AC_MSG_ERROR([You must supply an argument to --with-localmem])
	  ;;
	heapmem|tlsf)
	   localmem_allocator=$withval
	   ;;
	*)
	    AC_MSG_ERROR([--localmem-allocator=<heapmem | tlsf>])
	esac
    ])
AC_MSG_RESULT($localmem_allocator)

dnl Registry support in user-space (FUSE-based, default: off)

use_registry=
registry_root=
AC_MSG_CHECKING(whether the registry should be enabled)
AC_ARG_ENABLE(registry,
	AS_HELP_STRING([--enable-registry], [Export real-time objects to a registry]),
	[case "$enableval" in
	y | yes) use_registry=y; registry_root=/var/run/xenomai ;;
	/*) use_registry=y; registry_root=$enableval ;;
	*) unset use_registry ;;
	esac])
AC_MSG_RESULT(${use_registry:-no}${registry_root:+[,] mounted on ${registry_root}})

if test x$use_registry = xy; then
	PKG_CHECK_MODULES(FUSE, fuse)
	FUSE_CFLAGS="$FUSE_CFLAGS -DFUSE_USE_VERSION=25"
	AC_DEFINE(CONFIG_XENO_REGISTRY,1,[config])
	AC_DEFINE_UNQUOTED(CONFIG_XENO_REGISTRY_ROOT,"$registry_root",[config])
fi
AM_CONDITIONAL(XENO_REGISTRY,[test x$use_registry = xy])

dnl SMP support (default: on for cobalt/x86, off otherwise)

CONFIG_SMP=
if test $target_cpu_arch = x86 -a $rtcore_type = cobalt; then
	CONFIG_SMP=y
fi
AC_MSG_CHECKING(for SMP support)
AC_ARG_ENABLE(smp,
	AS_HELP_STRING([--enable-smp], [Enable SMP support]),
	[case "$enableval" in
	y | yes) CONFIG_SMP=y ;;
	*) unset CONFIG_SMP ;;
	esac])
AC_MSG_RESULT(${CONFIG_SMP:-no})

dnl Runtime sanity checks (default: on)

CONFIG_XENO_SANITY=y
AC_MSG_CHECKING(whether to enable sanity checks)
AC_ARG_ENABLE(sanity,
	AS_HELP_STRING([--enable-sanity], [Enable sanity checks at runtime]),
	[case "$enableval" in
	y | yes) CONFIG_XENO_SANITY=y ;;
	*) unset CONFIG_XENO_SANITY= ;;
	esac])
AC_MSG_RESULT(${CONFIG_XENO_SANITY:-no})

if test x$CONFIG_XENO_SANITY = xy; then
  AC_DEFINE(CONFIG_XENO_SANITY,1,[config])
else
  AC_DEFINE(CONFIG_XENO_SANITY,0,[config])
fi

dnl VSYSCALL (default: enabled) for Cobalt/x86

if test $target_cpu_arch = x86 -a $rtcore_type = cobalt; then
  CONFIG_XENO_X86_VSYSCALL=y
  AC_MSG_CHECKING(for x86 VSYSCALL availability)
  AC_ARG_ENABLE(x86-vsyscall,
	AS_HELP_STRING([--enable-x86-vsyscall], [Assume VSYSCALL enabled for issuing syscalls]),
	[case "$enableval" in
	y | yes) CONFIG_XENO_X86_VSYSCALL=y ;;
	*) unset CONFIG_XENO_X86_VSYSCALL ;;
	esac])
  AC_MSG_RESULT(${CONFIG_XENO_X86_VSYSCALL:-no})
fi

dnl Documentation package.

XENO_BUILD_DOC=
XENO_DOC_GIT=
AC_MSG_CHECKING(whether to build documentation)
AC_ARG_ENABLE(doc-build,
	AS_HELP_STRING([--enable-doc-build], [Build Xenomai documentation]),
	[case "$enableval" in
	y | yes) XENO_BUILD_DOC=y ;;
	n | no) ;;
	*) if test \! x$enableval = x; then
	      XENO_BUILD_DOC=y
	      XENO_DOC_GIT=$enableval
	   fi
	   ;;
	esac])
AM_CONDITIONAL(XENO_BUILD_DOC,[test "$XENO_BUILD_DOC" = y])
AC_SUBST(XENO_DOC_GIT)

AC_CHECK_PROG(DOXYGEN, doxygen, doxygen)

if test x${XENO_BUILD_DOC} = xy -a x"$DOXYGEN" = x ; then
   AC_MSG_ERROR([Missing the Doxygen tools to build the documentation])
fi

AC_CHECK_PROG(DOXYGEN_HAVE_DOT, dot, YES, NO)
if test x"$DOXYGEN_HAVE_DOT" = xYES ; then
   DOXYGEN_SHOW_INCLUDE_FILES=NO
else
   DOXYGEN_SHOW_INCLUDE_FILES=YES
fi

LATEX_BATCHMODE=YES
LATEX_MODE=batch
AC_MSG_CHECKING(for LaTeX mode)
AC_ARG_ENABLE(verbose-latex,
	AS_HELP_STRING([--enable-verbose-latex], [Disable LaTeX non-stop mode]),
	[case "$enableval" in
	y | yes)
	   LATEX_BATCHMODE=NO
	   LATEX_MODE=non-stop
	   ;;
	*) ;;
	esac])
AC_MSG_RESULT(${LATEX_MODE})

AC_CHECK_PROG(ASCIIDOC, asciidoc, asciidoc)
if test x${XENO_BUILD_DOC} = xy -a x"$ASCIIDOC" = x ; then
   AC_MSG_ERROR([Missing the asciidoc tool to build the documentation])
fi
AC_CHECK_PROG(A2X, a2x, a2x)
if test x${XENO_BUILD_DOC} = xy -a x"$A2X" = x ; then
   AC_MSG_ERROR([Missing the a2x tool to build the documentation])
fi
AC_CHECK_PROG(W3M, w3m, w3m)
if test x${XENO_BUILD_DOC} = xy -a x"$W3M" = x ; then
   AC_MSG_ERROR([Missing the w3m tool to build the documentation])
fi

dnl Set better default values for pdfdir, mandir and htmldir
dnl This won't override user settings, unless the user wants
dnl the default values, which we ban...

if test x$pdfdir = x'${docdir}'; then
   pdfdir='${docdir}/pdf'
fi
AC_SUBST(pdfdir)
if test x$mandir = x'${docdir}'; then
   mandir='${docdir}/man'
fi
AC_SUBST(mandir)
if test x$htmldir = x'${docdir}'; then
   htmldir='${docdir}/html'
fi
AC_SUBST(htmldir)

dnl Check for Valgrind client API support.
dnl Some GCC releases produce broken assembly code for Valgrind
dnl client calls, so we check this too. --disable-valgrind-client
dnl may be used to forcibly turn this API off.

AC_CHECK_HEADER(valgrind/valgrind.h,CONFIG_XENO_VALGRIND_API=y)

AC_MSG_CHECKING(for Valgrind client API)
AC_ARG_ENABLE(valgrind-client,
	AS_HELP_STRING([--enable-valgrind-client], [Enable Valgrind client API]),
	[case "$enableval" in
	n | no) unset CONFIG_XENO_VALGRIND_API ;;
	esac])
AC_MSG_RESULT(${CONFIG_XENO_VALGRIND_API:-no})

if test \! x$CONFIG_XENO_VALGRIND_API = x ; then
   AC_MSG_CHECKING([whether GCC emits sane code for Valgrind calls])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <valgrind/valgrind.h>]],
   				      [[return RUNNING_ON_VALGRIND;]])],
				      [ac_cv_valgrind_client=yes],
				      [ac_cv_valgrind_client="no (DISABLING)"])
   if [[ \! "$ac_cv_valgrind_client" = yes ]]; then
      unset CONFIG_XENO_VALGRIND_API
   fi
   AC_MSG_RESULT([$ac_cv_valgrind_client])
fi

test x$CONFIG_XENO_VALGRIND_API = xy && AC_DEFINE(CONFIG_XENO_VALGRIND_API,1,[config])

dnl Check for obstack support in *libc
AC_CHECK_HEADERS(obstack.h,libc_has_obstack=y)
AM_CONDITIONAL(XENO_PRIVATE_OBSTACK,[test x$libc_has_obstack = x])

dnl Check for presence of some headers
AC_CHECK_HEADERS(mqueue.h)

dnl Check for presence of some routines we need
save_LIBS="$LIBS"
LIBS="$LIBS -lrt -lpthread"
AC_CHECK_FUNCS([pthread_mutexattr_setprotocol	\
		pthread_mutexattr_getprotocol	\
		pthread_mutexattr_getprioceiling \
		pthread_mutexattr_setprioceiling \
		pthread_mutexattr_setrobust	\
		pthread_mutexattr_setrobust_np	\
		pthread_mutex_getprioceiling	\
		pthread_mutex_setprioceiling	\
		pthread_condattr_getclock	\
		pthread_condattr_setclock	\
		pthread_spin_lock fork		\
		pthread_attr_setaffinity_np	\
		pthread_setaffinity_np		\
		pthread_getattr_np		\
		pthread_atfork			\
		pthread_setname_np		\
		pthread_setschedprio		\
		sched_getcpu			\
		clock_nanosleep			\
		shm_open			\
		shm_unlink			\
		backtrace])
LIBS="$save_LIBS"

save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
AC_CHECK_DECLS([PTHREAD_PRIO_NONE], [], [], [#include <pthread.h>])
AC_CHECK_DECLS([PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP], [], [], [#include <pthread.h>])
AC_CHECK_DECLS([PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP], [], [], [#include <pthread.h>])
CPPFLAGS=$save_CPPFLAGS

dnl If we can't set the clock for condvar timeouts, then
dnl we have to restrict the Copperplate clock to CLOCK_REALTIME over
dnl Mercury unconditionally. Cobalt is different: although we may not
dnl have pthread_condattr_setclock() available from the threading library,
dnl Copperplate is still able to attach Cobalt condvars to specific clocks
dnl internally, therefore we don't have to use a restricted clock in
dnl Copperplate.
dnl
dnl In effect this means that updating the host system date may affect
dnl wait times of all blocking services implemented by Copperplate over
dnl Mercury, but will only affect explicit calls to pthread_cond_timedwait()
dnl over Cobalt.
dnl
dnl This is a provision for running over legacy threading libraries
dnl such as linuxthreads.
dnl
dnl CAUTION: the CLOCK_COPPERPLATE value is part of the ABI between
dnl the Xenomai core libraries and the applications. Therefore it shall
dnl remain stable even if applications depend on a different libc
dnl than Xenomai libraries were built against originally. Hence the
dnl built-in CONFIG_XENO_COPPERPLATE_CLOCK_RESTRICTED flag, which
dnl won't vary for a given Xenomai installation.

if test $rtcore_type = mercury; then
   AC_CHECK_FUNC(pthread_condattr_setclock,,
	 [AC_DEFINE(CONFIG_XENO_COPPERPLATE_CLOCK_RESTRICTED, 1,[config])])
fi

dnl Check that Copperplate can implement a shared heap if
dnl --enable-pshared was given.
if test x$use_pshared = xy;  then
  AC_CHECK_FUNC(shm_open,,
      [AC_MSG_ERROR([shm_open() is missing, --disable-pshared is required])])
fi

dnl
dnl Produce the info needed to build xeno_config.h
dnl

AC_DEFINE_UNQUOTED(CONFIG_XENO_VERSION_MAJOR,$CONFIG_XENO_VERSION_MAJOR,[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_VERSION_MINOR,$CONFIG_XENO_VERSION_MINOR,[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_REVISION_LEVEL,$CONFIG_XENO_REVISION_LEVEL,[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_UAPI_LEVEL,$CONFIG_XENO_UAPI_LEVEL,[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_VERSION_STRING,"$CONFIG_XENO_VERSION_STRING",[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_PREFIX,"$prefix",[config])
AC_DEFINE_UNQUOTED(CONFIG_XENO_BUILD_ARGS,"$XENO_BUILD_ARGS",[config])

dnl
dnl Features we enabled and likely want to find at kernel level.
dnl When applicable, we reuse the kernel option symbol so that we
dnl don't need to make particular cases with kernel code which may
dnl also be compiled in user-space libs.
dnl

test x$CONFIG_XENO_X86_VSYSCALL = xy && AC_DEFINE(CONFIG_XENO_X86_VSYSCALL,1,[config])
test x$CONFIG_SMP = xy && AC_DEFINE(CONFIG_SMP,1,[config])

dnl
dnl Userland may want to know about MMU availability on the target.
dnl For now, we assume that having fork() means having an MMU.
dnl
test x$ac_cv_func_fork = xyes && AC_DEFINE(CONFIG_MMU,1,[config])

AM_CONDITIONAL(CONFIG_XENO_SHARED,[test "$enable_shared" = 'yes'])

# Default sampling period (ns) used in various tests
AC_DEFINE_UNQUOTED(CONFIG_XENO_DEFAULT_PERIOD,$CONFIG_XENO_DEFAULT_PERIOD,[config])

dnl Allocator for Copperplate. Note: in dual kernel mode, we don't
dnl want malloc, no matter what: pick either heapmem or tlsf, defaults
dnl to heapmem. Force switch to malloc over the Mercury core in debug
dnl mode, to ease debugging with valgrind, instrumented glibc etc.

if test $rtcore_type = cobalt -o x$debug_mode = x; then
   case $localmem_allocator in
   heapmem)
	    AC_DEFINE(CONFIG_XENO_HEAPMEM,1,[config])
	    use_heapmem=y
	    use_tlsf=
   	    ;;
   tlsf)    
   	    AC_DEFINE(CONFIG_XENO_TLSF,1,[config])
	    use_tlsf=y
	    use_heapmem=
   	    ;;
   esac
else
	use_heapmem=
	use_tlsf=
        AC_MSG_WARN([using malloc() for private memory in debug mode])
fi
AM_CONDITIONAL(XENO_TLSF,[test x$use_tlsf = xy])
AM_CONDITIONAL(XENO_HEAPMEM,[test x$use_heapmem = xy])

dnl Check for atomic builtins. For now we only check for the legacy
dnl interface, i.e. __sync_*.

AC_CACHE_CHECK([whether the compiler provides atomic builtins], ac_cv_atomic_builtins, [
LIBS=
AC_TRY_LINK([
int atomic_sub(int i) { return __sync_sub_and_fetch(&i, 1); }
int atomic_add(int i) { return __sync_add_and_fetch(&i, 1); }
], [], ac_cv_atomic_builtins="yes")
])
if test "$ac_cv_atomic_builtins" != "yes"; then
   AC_MSG_ERROR([compiler does not support atomic builtins])
fi

unset want_fortify
AC_MSG_CHECKING(for fortify support)
AC_ARG_ENABLE([fortify],
	      AC_HELP_STRING([--enable-fortify],
			     [Enable _FORTIFY_SOURCE]),
	      [case "$enableval" in
	      y | yes) want_fortify=yes;;
	      *) want_fortify=no;;
	      esac])
AC_MSG_RESULT(${want_fortify:-autodetect})
AC_CHECK_FUNC(__vfprintf_chk,
      [AC_DEFINE(CONFIG_XENO_FORTIFY, 1,[config])],
      [if test x"$want_fortify" = "xyes"; then
      AC_MSG_ERROR([Fortify support enabled but not available in *libc])
      fi])

dnl Exported CFLAGS and LDFLAGS, shared with internal flags
XENO_USER_APP_CFLAGS="-D_GNU_SOURCE -D_REENTRANT -fasynchronous-unwind-tables"
XENO_USER_APP_LDFLAGS=

if test x$use_registry = xy; then
   XENO_FUSE_CFLAGS=$FUSE_CFLAGS
   XENO_USER_APP_LDFLAGS="$XENO_USER_APP_LDFLAGS $FUSE_LIBS"
fi

dnl Internal CFLAGS and LDFLAGS, may be enhanced per-arch below
XENO_USER_CFLAGS="$XENO_USER_APP_CFLAGS -pipe -fstrict-aliasing \
-Wall -Wstrict-prototypes -Wmissing-prototypes -Wno-long-long \
-Wno-unused-parameter -Wno-format-truncation -Werror -Wformat-security \
-D__XENO__ -D__IN_XENO__"
if test x$want_fortify = xyes -a x$debug_mode != xfull; then
   XENO_USER_CFLAGS="$XENO_USER_CFLAGS -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2"
fi
XENO_USER_LDADD="$XENO_USER_APP_LDFLAGS"

dnl Add any flags forced on the command line, but only
dnl for building apps.
XENO_USER_APP_CFLAGS="$CFLAGS $XENO_USER_APP_CFLAGS"
XENO_USER_APP_LDFLAGS="$LDFLAGS  $XENO_USER_APP_LDFLAGS"

if test x$debug_mode = xpartial; then
   XENO_USER_CFLAGS="-g -O2 $XENO_USER_CFLAGS"
elif test x$debug_mode = xfull; then
   XENO_USER_CFLAGS="-g -O0 $XENO_USER_CFLAGS"
elif test x$debug_symbols = xy; then
   XENO_USER_CFLAGS="-g -O2 $XENO_USER_CFLAGS"
else
   XENO_USER_CFLAGS="-O2 $XENO_USER_CFLAGS"
fi

if test x$use_assert = x; then
   XENO_USER_CFLAGS="-DNDEBUG $XENO_USER_CFLAGS"
fi

XENO_USER_CFLAGS_STDLIB="$XENO_USER_CFLAGS"
XENO_USER_CFLAGS="$XENO_USER_CFLAGS -I$topdir/include/$rtcore_type"

AC_MSG_CHECKING([whether ld supports @file])
AC_CACHE_VAL(ac_cv_ld_file_option,
  AC_LANG_SAVE
  AC_LANG_C
  save_LDFLAGS="$LDFLAGS"
  [LDFLAGS="-Wl,@/dev/null"]
  AC_LINK_IFELSE([AC_LANG_SOURCE([main(){}])],
    [ac_cv_ld_file_option=yes],
    [ac_cv_ld_file_option=no])
  LDFLAGS="$save_LDFLAGS"
  AC_LANG_RESTORE)
AC_MSG_RESULT(${ac_cv_ld_file_option:-no})
LD_FILE_OPTION=$ac_cv_ld_file_option
AC_SUBST(LD_FILE_OPTION)

AC_MSG_CHECKING(whether to enable dlopening of Xenomai libraries)
AC_ARG_ENABLE(dlopen-libs,
	AC_HELP_STRING([--enable-dlopen-libs], [Allow dynamic loading of Xenomai libraries]),
	[case "$enableval" in
	y | yes) CONFIG_XENO_LIBS_DLOPEN=y ;;
	*) CONFIG_XENO_LIBS_DLOPEN=$enableval ;;
	esac])
AC_MSG_RESULT(${CONFIG_XENO_LIBS_DLOPEN:-no})
if test x$CONFIG_XENO_LIBS_DLOPEN = xy; then
	AC_DEFINE(CONFIG_XENO_LIBS_DLOPEN,1,[config])
	AC_DEFINE_UNQUOTED(CONFIG_XENO_TLS_MODEL,"global-dynamic",[TLS model])
	XENO_LIB_LDFLAGS="-Wl,-z -Wl,nodelete"
else
	AC_DEFINE_UNQUOTED(CONFIG_XENO_TLS_MODEL,"initial-exec",[TLS model])
	XENO_LIB_LDFLAGS="-Wl,-z -Wl,nodlopen"
fi
AM_CONDITIONAL(CONFIG_XENO_LIBS_DLOPEN,[test x$CONFIG_XENO_LIBS_DLOPEN = xy])

AC_MSG_CHECKING(whether to enable TLS support)
AC_ARG_ENABLE([tls],
	    AC_HELP_STRING([--enable-tls],
			   [Enable thread local storage]),
	    [use_tls=$enableval])
AC_MSG_RESULT($use_tls)

dnl Check whether the compiler supports the __thread keyword.
if test "x$use_tls" != xno; then
	AC_CACHE_CHECK([for __thread keyword], libc_cv_gcc_tls,
	[cat > conftest.c <<\EOF
__thread int a __attribute__ ((tls_model ("initial-exec"))) = 42;
__thread int b __attribute__ ((tls_model ("global-dynamic"))) = 12;
EOF
	if AC_TRY_COMMAND([${CC-cc} $CFLAGS $CPPFLAGS -c -Werror conftest.c >&AS_MESSAGE_LOG_FD]); then
		libc_cv_gcc_tls=yes
	else
		libc_cv_gcc_tls=no
	fi
	rm -f conftest*])
	if test "$libc_cv_gcc_tls" = yes; then
		AC_DEFINE(HAVE_TLS,1,[config])
	fi
fi

AC_MSG_CHECKING(location for test executables)
AC_ARG_WITH(testdir,
    AS_HELP_STRING([--with-testdir=<test-exec-dir>],[location for test executables (defaults to $bindir)]),
    [
	case "$withval" in
	"" | y | ye | yes | n | no)
	    AC_MSG_ERROR([You must supply an argument to --with-testdir])
	  ;;
	esac
	XENO_TEST_DIR="$withval"
    ], [XENO_TEST_DIR=$bindir])
AC_MSG_RESULT($XENO_TEST_DIR)

demodir='${exec_prefix}/demo'
AC_MSG_CHECKING(location for demo programs)
AC_ARG_WITH(demodir,
    AS_HELP_STRING([--with-demodir=<demo-program-dir>],[location for demo programs (defaults to $prefix/demo)]),
    [
	case "$withval" in
	"" | y | ye | yes | n | no)
	    AC_MSG_ERROR([You must supply an argument to --with-demodir])
	  ;;
	esac
	XENO_DEMO_DIR="$withval"
    ], [XENO_DEMO_DIR=$demodir])
AC_MSG_RESULT($XENO_DEMO_DIR)

AC_MSG_CHECKING([for test source generation])
AC_RUN_IFELSE([AC_LANG_PROGRAM([[ ]], [[ ]])],
    [AC_MSG_RESULT(ok)], [AC_MSG_RESULT(failed)], [AC_MSG_RESULT(untestable)])

dnl CAUTION: We need to have the CONFIG_XENO_XX symbols always
dnl defined when the configuration header is read, but we want the
dnl Autoconf-produced symbols to be defined only when compiling
dnl Xenomai. This way, we won't pollute the namespace with the latter
dnl when our configuration header is indirectly included by a client
dnl application. To achieve this, we ask autoheader to produce the
dnl following header structure:
dnl #define CONFIG_XX
dnl #define CONFIG_XX ...
dnl #ifdef __IN_XENO__
dnl <Autoconf-defined symbols>
dnl #endif /* __IN_XENO__ */
dnl This is quite a hack since we have to rely on the fact that
dnl all Autoconf-generated symbols are lexicographically sorted
dnl after CONFIG_XENO_XX ones, but, well...
dnl Use a key which will cause the verbatim string to be put after
dnl all CONFIG_XENO_XX symbols, but still before any Autoconf-generated
dnl symbol, hence CONFIG_XENO___.
AH_VERBATIM(CONFIG_XENO___,[#ifdef __IN_XENO__])

dnl Now we can close the conditional section, right after all
dnl Autoconf-generated symbols have been listed.
AH_BOTTOM([#endif /* __IN_XENO__ */])

if test $rtcore_type = cobalt; then
   XENO_USER_CFLAGS="-I$topdir/lib/cobalt/arch/$target_cpu_arch/include -I$topdir/kernel/cobalt/arch/$target_cpu_arch/include $XENO_USER_CFLAGS"
   XENO_COBALT_CFLAGS="$XENO_USER_CFLAGS"
   case "$build_for" in
    i*86*-*) XENO_COBALT_CFLAGS="$XENO_COBALT_CFLAGS -fno-omit-frame-pointer";;
    esac

dnl Build wrapping information. XENO_POSIX_WRAPPERS lists all wrapping
dnl directives in a format the linker understands, for building the
dnl in-tree executables which require POSIX symbol wrapping.

   modechk_wrappers="$topdir/lib/cobalt/modechk.wrappers"
   cobalt_wrappers="$topdir/lib/cobalt/cobalt.wrappers"
   if [[ $ac_cv_ld_file_option = yes ]]; then
	XENO_POSIX_WRAPPERS="-Wl,@$modechk_wrappers -Wl,@$cobalt_wrappers"
   else
	XENO_POSIX_WRAPPERS=`cat $modechk_wrappers $cobalt_wrappers | \
			while read wrap_option symbol ; do \
				echo -n "-Wl,$wrap_option,$symbol " ; \
			done`
   fi

   AC_SUBST(XENO_POSIX_WRAPPERS)
   AC_SUBST([CONFIG_STATUS_DEPENDENCIES], ["$modechk_wrappers $cobalt_wrappers"])
fi

dnl Multi-library support.
AC_MSG_CHECKING([whether to enable soname suffix for libraries])
AC_ARG_ENABLE([so-suffix],
   [AS_HELP_STRING([--enable-so-suffix],
      [enable soname suffix (for Mercury only)])],
   [enable_so_suffix=$enableval],
   [enable_so_suffix="no"])
AC_MSG_RESULT(${enable_so_suffix})
if test "$enable_so_suffix" = "yes"; then
   if test "$rtcore_type" != mercury; then
      AC_MSG_ERROR([soname suffix is only allowed for Mercury core])
   else
      CORE="_$rtcore_type"
   fi
fi

dnl
dnl Build the Makefiles
dnl

XENO_AUTOINIT_LDFLAGS='$(top_builddir)/lib/boilerplate/init/bootstrap-internal.o'" -Wl,--wrap=main -Wl,--dynamic-list=$topdir/scripts/dynlist.ld"
AC_SUBST(XENO_AUTOINIT_LDFLAGS)

XENO_CORE_LDADD="\$(top_builddir)/lib/$rtcore_type/lib${rtcore_type}.la"
if test $rtcore_type = cobalt; then
   XENO_CORE_LDADD="$XENO_CORE_LDADD \$(top_builddir)/lib/cobalt/libmodechk.la"
fi
AC_SUBST(XENO_CORE_LDADD)

AC_SUBST(DOXYGEN_SHOW_INCLUDE_FILES)
AC_SUBST(DOXYGEN_HAVE_DOT)
AC_SUBST(DOXYGEN)
AC_SUBST(LATEX_BATCHMODE)
AC_SUBST(LATEX_MODE)

AC_SUBST(ASCIIDODC)
AC_SUBST(A2X)
AC_SUBST(W3M)

AC_SUBST(XENO_TARGET_CORE)
AC_SUBST(XENO_TARGET_ARCH)
AC_SUBST(XENO_BUILD_STRING)
AC_SUBST(XENO_HOST_STRING)
AC_SUBST(XENO_COBALT_CFLAGS)
AC_SUBST(XENO_LIB_LDFLAGS)
AC_SUBST(XENO_USER_CFLAGS)
AC_SUBST(XENO_USER_CFLAGS_STDLIB)
AC_SUBST(XENO_USER_LDADD)
AC_SUBST(XENO_USER_APP_CFLAGS)
AC_SUBST(XENO_USER_APP_LDFLAGS)
AC_SUBST(XENO_FUSE_CFLAGS)
AC_SUBST(XENO_TEST_DIR)
AC_SUBST(XENO_DEMO_DIR)
AC_SUBST(XENO_BUILD_COMPILER)
AC_SUBST(XENO_BUILD_ARGS)
AC_SUBST(CORE)

AC_CONFIG_FILES([ \
	Makefile \
	config/Makefile \
	scripts/Makefile \
	scripts/xeno-config:scripts/xeno-config-$rtcore_type.in \
	scripts/xeno \
	lib/Makefile \
	lib/boilerplate/Makefile \
	lib/boilerplate/init/Makefile \
	lib/cobalt/Makefile \
	lib/cobalt/arch/Makefile \
	lib/cobalt/arch/arm/Makefile \
	lib/cobalt/arch/arm/include/Makefile \
	lib/cobalt/arch/arm/include/asm/Makefile \
	lib/cobalt/arch/arm/include/asm/xenomai/Makefile \
	lib/cobalt/arch/arm64/Makefile \
	lib/cobalt/arch/arm64/include/Makefile \
	lib/cobalt/arch/arm64/include/asm/Makefile \
	lib/cobalt/arch/arm64/include/asm/xenomai/Makefile \
	lib/cobalt/arch/powerpc/Makefile \
	lib/cobalt/arch/powerpc/include/Makefile \
	lib/cobalt/arch/powerpc/include/asm/Makefile \
	lib/cobalt/arch/powerpc/include/asm/xenomai/Makefile \
	lib/cobalt/arch/x86/Makefile \
	lib/cobalt/arch/x86/include/Makefile \
	lib/cobalt/arch/x86/include/asm/Makefile \
	lib/cobalt/arch/x86/include/asm/xenomai/Makefile \
	lib/mercury/Makefile \
	lib/copperplate/Makefile \
	lib/copperplate/regd/Makefile \
	lib/alchemy/Makefile \
	lib/vxworks/Makefile \
	lib/psos/Makefile \
	lib/analogy/Makefile \
	lib/smokey/Makefile \
	lib/trank/Makefile \
	testsuite/Makefile \
	testsuite/latency/Makefile \
	testsuite/switchtest/Makefile \
	testsuite/gpiotest/Makefile \
	testsuite/gpiobench/Makefile \
	testsuite/spitest/Makefile \
	testsuite/smokey/Makefile \
	testsuite/smokey/arith/Makefile \
	testsuite/smokey/dlopen/Makefile \
	testsuite/smokey/sched-quota/Makefile \
	testsuite/smokey/sched-tp/Makefile \
	testsuite/smokey/setsched/Makefile \
	testsuite/smokey/rtdm/Makefile \
	testsuite/smokey/vdso-access/Makefile \
	testsuite/smokey/posix-cond/Makefile \
	testsuite/smokey/posix-mutex/Makefile \
	testsuite/smokey/posix-clock/Makefile \
	testsuite/smokey/posix-fork/Makefile \
	testsuite/smokey/posix-select/Makefile \
	testsuite/smokey/xddp/Makefile \
	testsuite/smokey/iddp/Makefile \
	testsuite/smokey/bufp/Makefile \
	testsuite/smokey/sigdebug/Makefile \
	testsuite/smokey/timerfd/Makefile \
	testsuite/smokey/tsc/Makefile \
	testsuite/smokey/leaks/Makefile \
	testsuite/smokey/memcheck/Makefile \
	testsuite/smokey/memory-coreheap/Makefile \
	testsuite/smokey/memory-heapmem/Makefile \
	testsuite/smokey/memory-tlsf/Makefile \
	testsuite/smokey/memory-pshared/Makefile \
	testsuite/smokey/fpu-stress/Makefile \
	testsuite/smokey/net_udp/Makefile \
	testsuite/smokey/net_packet_dgram/Makefile \
	testsuite/smokey/net_packet_raw/Makefile \
	testsuite/smokey/net_common/Makefile \
	testsuite/smokey/cpu-affinity/Makefile \
	testsuite/smokey/gdb/Makefile \
	testsuite/smokey/y2038/Makefile \
	testsuite/clocktest/Makefile \
	testsuite/xeno-test/Makefile \
	utils/Makefile \
	utils/hdb/Makefile \
	utils/can/Makefile \
	utils/analogy/Makefile \
	utils/ps/Makefile \
	utils/slackspot/Makefile \
	utils/corectl/Makefile \
	utils/autotune/Makefile \
	utils/net/rtnet \
	utils/net/rtnet.conf \
	utils/net/Makefile \
	utils/chkkconf/Makefile \
	demo/Makefile \
	demo/posix/Makefile \
	demo/posix/cyclictest/Makefile \
	demo/posix/cobalt/Makefile \
	demo/alchemy/Makefile \
	demo/alchemy/cobalt/Makefile \
	include/Makefile \
	include/cobalt/uapi/Makefile \
	include/cobalt/uapi/asm-generic/Makefile \
	include/cobalt/uapi/kernel/Makefile \
	include/cobalt/Makefile \
	include/cobalt/sys/Makefile \
	include/cobalt/kernel/Makefile \
	include/cobalt/kernel/rtdm/Makefile \
	include/cobalt/kernel/rtdm/analogy/Makefile \
	include/cobalt/boilerplate/Makefile \
	include/rtdm/Makefile \
	include/rtdm/uapi/Makefile \
	include/mercury/Makefile \
	include/mercury/boilerplate/Makefile \
	include/boilerplate/Makefile \
	include/copperplate/Makefile \
	include/alchemy/Makefile \
	include/vxworks/Makefile \
	include/psos/Makefile \
	include/smokey/Makefile \
	include/trank/Makefile \
	include/trank/posix/Makefile \
	include/trank/native/Makefile \
	include/trank/rtdm/Makefile \
	include/xenomai/Makefile \
	doc/Makefile \
	doc/doxygen/Makefile \
	doc/doxygen/xeno3prm-common.conf \
	doc/doxygen/xeno3prm-html.conf \
	doc/doxygen/xeno3prm-latex.conf \
	doc/gitdoc/Makefile \
	doc/asciidoc/Makefile \
	])

AC_OUTPUT()
