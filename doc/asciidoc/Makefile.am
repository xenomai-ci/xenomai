HTML_DOCS = 					\
	html/MIGRATION				\
	html/README.APPLICATIONS		\
	html/README.INSTALL			\
	html/TROUBLESHOOTING.COBALT		\
	html/TROUBLESHOOTING.MERCURY		\
	html/asciidoc-icons			\
	html/asciidoc-icons/callouts		\
	html/man1/autotune			\
	html/man1/clocktest			\
	html/man1/corectl			\
	html/man1/dohell			\
	html/man1/latency			\
	html/man1/rtcanconfig			\
	html/man1/rtcanrecv			\
	html/man1/rtcansend			\
	html/man1/slackspot			\
	html/man1/switchtest			\
	html/man1/xeno				\
	html/man1/xeno-config			\
	html/man1/xeno-test

PDF_DOCS = 				\
	MIGRATION.pdf			\
	README.APPLICATIONS.pdf 	\
	README.INSTALL.pdf 		\
	TROUBLESHOOTING.COBALT.pdf	\
	TROUBLESHOOTING.MERCURY.pdf

TXT_DOCS =				\
	MIGRATION.txt			\
	README.APPLICATIONS.txt 	\
	README.INSTALL.txt		\
	TROUBLESHOOTING.COBALT.txt	\
	TROUBLESHOOTING.MERCURY.txt

MAN1_DOCS = 			\
	man1/autotune.1		\
	man1/clocktest.1 	\
	man1/corectl.1	 	\
	man1/cyclictest.1 	\
	man1/dohell.1		\
	man1/latency.1 		\
	man1/rtcanconfig.1 	\
	man1/rtcanrecv.1 	\
	man1/rtcansend.1 	\
	man1/slackspot.1	\
	man1/switchtest.1 	\
	man1/xeno-config.1 	\
	man1/xeno-test.1 	\
	man1/xeno.1

EXTRA_DIST :=				\
	MIGRATION.adoc 			\
	README.APPLICATIONS.adoc	\
	README.INSTALL.adoc 		\
	TROUBLESHOOTING.COBALT.adoc	\
	TROUBLESHOOTING.MERCURY.adoc 	\
	plaintext.conf 			\
	plaintext.xsl			\
	plaintext_postproc.awk		\
	$(MAN1_DOCS:%.1=%.adoc)

if XENO_BUILD_DOC

HTML_DOCSDIR = ./
PDF_DOCSDIR = ./
MAN_DOCSDIR = ./

ASCIIDOC_HTML_OPTS=-a icons -a iconsdir=../asciidoc-icons \
	-a toc -a toclevels=3 -a max-width=55em -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_PDF_OPTS=-a icons -a toc -a toclevels=3 -a xenover=$(PACKAGE_VERSION)

ASCIIDOC_MAN_OPTS=-a xenover=$(PACKAGE_VERSION)

ASCIIDOC_TXT_OPTS=-a xenover=$(PACKAGE_VERSION) -a encoding=ascii

tmpdir=adoc_plaintext

all-local: $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(MAN1_DOCS)

html/%: %.adoc Makefile
	@$(mkdir_p) $@
	$(ASCIIDOC) -n -b xhtml11 $(ASCIIDOC_HTML_OPTS) -o $@/index.html $<

%.1: %.adoc Makefile
	@$(mkdir_p) man1
	$(A2X) -f manpage -D man1 $(ASCIIDOC_MAN_OPTS) $<

%.pdf: %.adoc Makefile
	$(A2X) -f pdf -D . $(ASCIIDOC_PDF_OPTS) $<

$(tmpdir)/%.txt: %.adoc Makefile plaintext.conf plaintext.xsl
	@$(mkdir_p) $(tmpdir)
	$(ASCIIDOC) --backend docbook -f $(srcdir)/plaintext.conf \
		--doctype article $(ASCIIDOC_TXT_OPTS) \
		--out-file $(tmpdir)/$*.xml $<
	xsltproc --stringparam toc.section.depth 3 --nonet \
		--output $(tmpdir)/$*.html $(srcdir)/plaintext.xsl \
		$(tmpdir)/$*.xml
	w3m -cols 80 -dump -T text/html -no-graph $(tmpdir)/$*.html > $@

%.txt: $(tmpdir)/%.txt Makefile plaintext_postproc.awk
	awk -f $(srcdir)/plaintext_postproc.awk $(tmpdir)/$*.txt > $@

html/asciidoc-icons:
	$(RM) -R asciidoc-icons
	@if test -d /usr/share/doc/asciidoc/images/; then \
		cp -a /usr/share/doc/asciidoc/images/icons/ html/asciidoc-icons; \
	elif test -d /usr/share/asciidoc/images/icons/; then \
		cp -a /usr/share/asciidoc/images/icons/ html/asciidoc-icons; \
	else \
		cp -a /etc/asciidoc/images/icons/ html/asciidoc-icons; \
	fi

html/asciidoc-icons/callouts: html/asciidoc-icons

.PHONY: html/asciidoc-icons

include $(top_srcdir)/doc/install.rules

install-data-local: install-docs-local
uninstall-local: uninstall-docs

else
install-data-local:
uninstall-local:
endif

distclean-local: clean-local

clean-local:
	$(RM) -R $(HTML_DOCS) $(PDF_DOCS) $(TXT_DOCS) $(tmpdir)
