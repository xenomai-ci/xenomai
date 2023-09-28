menuconfig XENOMAI
	depends on X86_TSC || !X86
	bool "Xenomai/cobalt"
	select IPIPE if HAVE_IPIPE_SUPPORT
	select IPIPE_WANT_APIREV_2 if IPIPE
	select DOVETAIL if HAVE_DOVETAIL
	select DOVETAIL_LEGACY_SYSCALL_RANGE if HAVE_DOVETAIL
	default y
	help
	  Xenomai's Cobalt core is a real-time extension to the Linux
	  kernel, which exhibits very short interrupt and scheduling
	  latency, without affecting the regular kernel services.

	  This option enables the set of extended kernel services
	  required to run the real-time applications in user-space,
	  over the Xenomai libraries.

	  Please visit http://xenomai.org for more information.

if XENOMAI
source "arch/@SRCARCH@/xenomai/Kconfig"
endif

if MIGRATION
comment "WARNING! Page migration (CONFIG_MIGRATION) may increase"
comment "latency."
endif

if APM || CPU_FREQ || ACPI_PROCESSOR || INTEL_IDLE
comment "WARNING! At least one of APM, CPU frequency scaling, ACPI 'processor'"
comment "or CPU idle features is enabled. Any of these options may"
comment "cause troubles with Xenomai. You should disable them."
endif

config XENO_VERSION_MAJOR
       int
       default @VERSION_MAJOR@

config XENO_VERSION_MINOR
       int
       default @VERSION_MINOR@

config XENO_REVISION_LEVEL
       int
       default @REVISION_LEVEL@

config XENO_VERSION_STRING
       string
       default "@VERSION_STRING@"

config KVER_SUBLEVEL
       int
       default @KVER_SUBLEVEL@
