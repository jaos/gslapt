## Process this file with automake to produce Makefile.in

sbin_PROGRAMS = gslapt

gslapt_SOURCES = \
	main.c \
	support.c support.h \
	interface.c interface.h \
	callbacks.c callbacks.h \
	series.c series.h \
	settings.c settings.h

gslapt_LDADD = @GLIB_LIBS@ @GTK_LIBS@ $(INTLLIBS)
gslapt_CPPFLAGS = @GLIB_CFLAGS@ @GTK_CFLAGS@ \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

