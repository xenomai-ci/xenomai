
data_DATA = kconf-checklist

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

sbin_PROGRAMS = chkkconf

chkkconf_SOURCES = checkconfig.c

chkkconf_CPPFLAGS = 		\
	-DDATADIR=\"$(datadir)\" -D_GNU_SOURCE
