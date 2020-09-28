demodir = @XENO_DEMO_DIR@

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

demo_PROGRAMS = cross-link

cppflags = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

ldadd = 					\
	@XENO_AUTOINIT_LDFLAGS@			\
	$(XENO_POSIX_WRAPPERS)			\
	../../../lib/alchemy/libalchemy@CORE@.la 	\
	../../../lib/copperplate/libcopperplate@CORE@.la \
	@XENO_CORE_LDADD@			\
	@XENO_USER_LDADD@			\
	-lrt -lpthread -lm

cross_link_SOURCES = cross-link.c
cross_link_CPPFLAGS = $(cppflags)
cross_link_LDADD = $(ldadd)
