## Process this file with automake to produce Makefile.in

SUBDIRS = src po
SLACKPKGBUILDIR=/tmp/gslapt_build_dir

EXTRA_DIST = \
	autogen.sh \
	gslapt.glade \
	gslapt.gladep

install-data-local:
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)/usr/share/applications; \
	$(mkinstalldirs) $(DESTDIR)/usr/share/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) AUTHORS ChangeLog COPYING FAQ README $(DESTDIR)/usr/share/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) gslapt.desktop $(DESTDIR)/usr/share/applications/gslapt.desktop; \
	$(mkinstalldirs) $(DESTDIR)/usr/share/pixmaps; \
	$(INSTALL_DATA) pixmaps/gslapt.png $(DESTDIR)/usr/share/pixmaps/; \
	if test -d $(srcdir)/pixmaps; then \
	  $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/pixmaps; \
	  for pixmap in $(srcdir)/pixmaps/*; do \
	    if test -f $$pixmap; then \
	      $(INSTALL_DATA) $$pixmap $(DESTDIR)$(pkgdatadir)/pixmaps; \
	    fi \
	  done \
	fi

dist-hook:
	if test -d pixmaps; then \
	  mkdir $(distdir)/pixmaps; \
	  for pixmap in pixmaps/*; do \
	    if test -f $$pixmap; then \
	      cp -p $$pixmap $(distdir)/pixmaps; \
	    fi \
	  done \
	fi

slackpkg: all
	@make install DESTDIR=$(SLACKPKGBUILDIR)
	cp slack-desc $(SLACKPKGBUILDIR)
	cp slack-required $(SLACKPKGBUILDIR)
	chown root:bin $(SLACKPKGBUILDIR)/$(sbindir)
	chown root:bin $(SLACKPKGBUILDIR)/$(sbindir)/@PACKAGE@
	( cd $(SLACKPKGBUILDIR) && /sbin/makepkg -l y -c n @PACKAGE@-@VERSION@-i386-1.tgz )

