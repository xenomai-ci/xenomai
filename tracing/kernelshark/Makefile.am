libsub_LTLIBRARIES = plugin_xenomai_cobalt_switch_events.la

plugin_xenomai_cobalt_switch_events_la_SOURCES =	\
	CobaltSwitchEvents.cpp				\
	xenomai_cobalt_switch_events.c			\
	xenomai_cobalt_switch_events.h

plugin_xenomai_cobalt_switch_events_la_CPPFLAGS  =	\
	$(LIBKSHARK_CFLAGS)

plugin_xenomai_cobalt_switch_events_la_LDFLAGS =	\
	$(LIBKSHARK_LIBS)				\
	-module -avoid-version

AM_LIBTOOLFLAGS = --silent --tag=disable-static

libsubdir := $(libdir)/kernelshark/plugins

install-data-hook:
	$(RM) $(DESTDIR)$(libsubdir)/$(libsub_LTLIBRARIES)
