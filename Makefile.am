ACLOCAL_AMFLAGS=-I config

SUBDIRS = 		\
	doc		\
	lib 		\
	config		\
	include		\
	scripts		\
	utils

if XENO_ENABLE_DEMO
SUBDIRS += 		\
	demo
endif

if XENO_ENABLE_TESTSUITE
SUBDIRS += 		\
	testsuite
endif

SUBDIRS +=		\
	tracing

EXTRA_DIST = kernel debian

DIST_SUBDIRS =		\
	config		\
	demo		\
	doc		\
	include		\
	lib 		\
	tracing		\
	scripts		\
	testsuite	\
	utils

doc/%: FORCE
	$(MAKE) -C doc/ $*

dist-hook:
	rm -fr `find $(distdir) -name '.svn' -o -name CVS  -o -name '.#*' \
		-o -name '*~' -o -name autom4te.cache`

install-udev-rules:
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

uninstall-udev-rules:
if XENO_COBALT
	cd $(srcdir)/kernel/cobalt/udev && for f in *.rules; do \
	    rm -f $(DESTDIR)/$(sysconfdir)/udev/rules.d/$$f ; \
	done
endif

# legacy alias
install-user: install

install-exec-local: install-udev-rules

uninstall-local: uninstall-udev-rules

uninstall-local:
if XENO_COBALT
	cd $(srcdir)/kernel/cobalt/udev ; \
	for f in *.rules ; do \
	    $(RM) $(DESTDIR)$(sysconfdir)/udev/rules.d/$$f ; \
	done
endif

.PHONY: FORCE
