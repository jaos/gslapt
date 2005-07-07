## Process this file with automake to produce Makefile.in

SUBDIRS = src po

EXTRA_DIST = \
	autogen.sh \
	gslapt.glade \
	gslapt.gladep

install-data-local:
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/applications; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) AUTHORS ChangeLog COPYING FAQ README $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) gslapt.desktop $(DESTDIR)/$(datadir)/applications/gslapt.desktop; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/pixmaps; \
	$(INSTALL_DATA) pixmaps/gslapt.png $(DESTDIR)/$(datadir)/pixmaps/; \
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

pkg: all
	@make install DESTDIR=`pwd`/pkg
	$(mkinstalldirs) pkg/install
	install slack-desc pkg/install/
	install slack-required pkg/install/
	-chown root:bin pkg/$(sbindir)
	-chown root:bin pkg/$(sbindir)/@PACKAGE@
	@echo -n DESKTOPFILE= > pkg/install/doinst.sh
	@echo "$(DESTDIR)/$(datadir)/applications/gslapt.desktop" |sed -re "s/^\/+//" >> pkg/install/doinst.sh
	@echo 'if [ -f $$DESKTOPFILE ]; then if [ -x usr/bin/gksudo ]; then sed -i.bak -re "s/(Exec=)(\/usr\/sbin\/gslapt)/\1gksudo \2/" $$DESKTOPFILE; elif [ -x usr/bin/gksu ]; then sed -i.bak -re "s/(Exec=)(\/usr\/sbin\/gslapt)/\1gksu \2/" $$DESKTOPFILE; elif [ -x usr/bin/gnomesu ]; then sed -i.bak -re "s/(Exec=)(\/usr\/sbin\/gslapt)/\1gnomesu \2/" $$DESKTOPFILE; elif [ -x usr/bin/kdesu ]; then sed -i.bak -re "s/(Exec=)(\/usr\/sbin\/gslapt)/\1kdesu \2/" $$DESKTOPFILE; fi; fi' >> pkg/install/doinst.sh
	strip --strip-unneeded pkg/$(sbindir)/@PACKAGE@
	( cd pkg && /sbin/makepkg -l y -c n @PACKAGE@-@VERSION@-@host_cpu@-1.tgz )

