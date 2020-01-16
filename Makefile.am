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

dev devices:
if XENO_COBALT
	@$(mkinstalldirs) $(DESTDIR)/dev; \
	if test x`id -u` = x0; then \
	   sudo= ; \
	else if test \! "x`type $(SUDO)`" = x ; then \
	   echo "sudo found (sudo is $(SUDO)), testing it..." ; \
	   $(SUDO) sh -c "( mknod $(DESTDIR)/dev/dummy c 150 0 && rm -f $(DESTDIR)/dev/dummy )" && \
	   sudo="$(SUDO)" || { \
echo '*** Warning: you are not super-user and sudo does not work, real-time devices' ; \
echo '*** nodes will not be created.' ; \
echo '*** If you do not create these devices by hand, Xenomai test programs will not' ; \
echo '*** work.' ; \
echo '*** To create these devices please run "make devices" with super-user' ; \
echo '*** privileges.' ; \
	     exit 0 ; } ; \
	   echo 'sudo is working.' ; \
	   fi ; \
	fi ; \
	if test -r $(DESTDIR)/etc/udev/udev.rules ; then \
	   for f in $(srcdir)/kernel/cobalt/udev/*.rules ; do \
	   b=`basename $$f` ; \
	   grep -q Xenomai:`basename $$b .rules` $(DESTDIR)/etc/udev/udev.rules || \
	   $$sudo sh -c "( echo ; cat $$f ) >> $(DESTDIR)/etc/udev/udev.rules" ; \
	   done ; \
	else if test -d $(DESTDIR)/etc/udev/rules.d ; then \
	      for f in $(srcdir)/kernel/cobalt/udev/*.rules ; do \
		 $$sudo cp $$f $(DESTDIR)/etc/udev/rules.d/ ; \
	      done ; \
	   fi ; \
	fi ; \
	for n in `seq 0 31` ; do \
	   f="$(DESTDIR)/dev/rtp$$n" ; \
	   if test \! -c $$f ; then \
	      $$sudo mknod -m 666 $$f c 150 $$n ; \
	   fi ; \
	done
endif

install-exec-local: devices

install-user:
	$(MAKE) SUDO=false install

uninstall-local:
if XENO_COBALT
	cd $(srcdir)/kernel/cobalt/udev ; \
	for f in *.rules ; do \
	    $(RM) $(DESTDIR)$(sysconfdir)/udev/rules.d/$$f ; \
	done
endif

.PHONY: FORCE
