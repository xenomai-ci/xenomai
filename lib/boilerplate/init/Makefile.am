noinst_LIBRARIES = libbootstrap.a

libbootstrap_a_SOURCES = bootstrap.c

libbootstrap_a_CPPFLAGS =		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

noinst_LTLIBRARIES = libbootstrap-pic.la

libbootstrap_pic_la_SOURCES = bootstrap.c

libbootstrap_pic_la_CPPFLAGS =		\
	-D__BOOTSTRAP_DSO__		\
	@XENO_USER_CFLAGS@		\
	-I$(top_srcdir)/include		\
	-I$(top_srcdir)/lib

get_pic_object = $(shell source ./$(libbootstrap_pic_la_OBJECTS) && echo $$pic_object)

all-local: $(libbootstrap_a_OBJECTS)
	@cp $< bootstrap-internal.o

install-data-local:
	@$(mkinstalldirs) $(DESTDIR)$(libdir)/xenomai
	@$(INSTALL_DATA) $(libbootstrap_a_OBJECTS) $(DESTDIR)$(libdir)/xenomai/bootstrap.o
	@test -r $(call get_pic_object) && \
	$(INSTALL_DATA) $(call get_pic_object) $(DESTDIR)$(libdir)/xenomai/bootstrap-pic.o || true

uninstall-local:
	$(RM) $(DESTDIR)$(libdir)/xenomai/bootstrap*.o
