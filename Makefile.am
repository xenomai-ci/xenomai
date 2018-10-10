ACLOCAL_AMFLAGS=-I config

SUDO=sudo

SUBDIRS = 		\
	doc		\
	lib 		\
	config		\
	include		\
	scripts		\
	testsuite	\
	utils		\
	demo

EXTRA_DIST = kernel debian

DIST_SUBDIRS =		\
	config		\
	demo		\
	doc		\
	include		\
	lib 		\
	scripts		\
	testsuite	\
	utils

doc/%: FORCE
	$(MAKE) -C doc/ $*

dist-hook:
	rm -fr `find $(distdir) -name '.svn' -o -name CVS  -o -name '.#*' \
		-o -name '*~' -o -name autom4te.cache`

udev-rules:
if XENO_COBALT
	if test -r $(DESTDIR)/$(sysconfdir)/udev/udev.rules ; then \
	    for f in $(srcdir)/kernel/cobalt/udev/*.rules ; do \
		b=`basename $$f` ; \
		grep -q Xenomai:`basename $$b .rules` $(DESTDIR)/$(sysconfdir)/udev/udev.rules || \
		( echo ; cat $$f ) >> $(DESTDIR)/$(sysconfdir)/udev/udev.rules ; \
	    done ; \
	else \
	    $(mkinstalldirs) $(DESTDIR)/$(sysconfdir)/udev/rules.d; \
	    for f in $(srcdir)/kernel/cobalt/udev/*.rules ; do \
		$(INSTALL_DATA) $$f $(DESTDIR)/$(sysconfdir)/udev/rules.d/ ; \
	    done ; \
	fi
endif

install-exec-local: udev-rules

.PHONY: FORCE
