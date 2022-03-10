libsub_LTLIBRARIES = plugin_xenomai_schedparams.la

plugin_xenomai_schedparams_la_SOURCES =	\
	plugin_xenomai_schedparams.c

plugin_xenomai_schedparams_la_CPPFLAGS =	\
	$(LIBTRACEEVENT_CFLAGS)			\
	-I$(top_srcdir)/include

plugin_xenomai_schedparams_la_LDFLAGS =		\
	$(LIBTRACEEVENT_LIBS)			\
	-module -avoid-version -nostartfiles

AM_LIBTOOLFLAGS = --silent --tag=disable-static

libsubdir := $(libdir)/traceevent/plugins

install-data-hook:
	$(RM) $(DESTDIR)$(libsubdir)/$(libsub_LTLIBRARIES)
