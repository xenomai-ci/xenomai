sbin_PROGRAMS = hdb

hdb_SOURCES = hdb.c

hdb_CPPFLAGS = 			\
	$(XENO_USER_CFLAGS)	\
	-I$(top_srcdir)/include

hdb_LDFLAGS = @XENO_AUTOINIT_LDFLAGS@

hdb_LDADD =					\
	../../lib/copperplate/libcopperplate@CORE@.la	\
	@XENO_CORE_LDADD@			\
	@XENO_USER_LDADD@			\
	-lpthread -lrt
