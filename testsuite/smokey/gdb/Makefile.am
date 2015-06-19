
noinst_LIBRARIES = libgdb.a

libgdb_a_SOURCES = gdb.c

CCLD = $(top_srcdir)/scripts/wrap-link.sh $(CC)

libgdb_a_CPPFLAGS = 		\
	@XENO_USER_CFLAGS@	\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/include

libgdb_a_CXXFLAGS = -g -O0
