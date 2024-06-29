HTML_DOCS = html/xeno3prm html/xeno3prm/search
PDF_DOCS  = xeno3prm.pdf
EXTRA_DIST = xeno3prm-common.conf.in xeno3prm-html.conf.in xeno3prm-latex.conf.in
CLEANFILES = xeno3prm-common.conf xeno3prm-html.conf xeno3prm-latex.conf

if XENO_BUILD_DOC

HTML_DOCSDIR = ./
PDF_DOCSDIR = ./

all-local: html pdf

html/xeno3prm/search: html/xeno3prm

html: $(HTML_DOCS)

pdf: $(PDF_DOCS)

html/xeno3prm latex/xeno3prm: FORCE
	@mkdir -p $@
	$(DOXYGEN) $(@F)-$(@D).conf

%.pdf: latex/%
	$(MAKE) -C $< refman.pdf
	mv $</refman.pdf $@

distclean-local:
	for dir in *-html *-latex; do \
	    if test -d $$dir ; then $(RM) -R $$dir ; fi ; \
	done

.PHONY: FORCE

.DELETE_ON_ERROR:

include $(top_srcdir)/doc/install.rules

install-data-local: install-docs-local
uninstall-local: uninstall-docs

else
install-data-local:
uninstall-local:
endif
