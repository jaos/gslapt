## Process this file with automake to produce Makefile.in

SUBDIRS = src po ui

PKGDIR = `pwd`/pkg
PKGREL = 1
if POLKIT
polkit_actiondir = $(datadir)/polkit-1/actions
INSTALL_GSLAPT_POLKIT = mkdir -p $(DESTDIR)/$(bindir); echo -e "\#!/bin/sh\npkexec --disable-internal-agent $(sbindir)/gslapt \$$@" > $(DESTDIR)/$(bindir)/gslapt-polkit; chmod +x $(DESTDIR)/$(bindir)/gslapt-polkit; $(mkinstalldirs) $(DESTDIR)/$(polkit_actiondir); $(INSTALL_DATA) org.jaos.gslapt.policy $(DESTDIR)/$(polkit_actiondir)/org.jaos.gslapt.policy; sed -i -re 's,(exec\.path">).*(<\/annotate>),\1$(sbindir)/@PACKAGE@\2,g' $(DESTDIR)/$(polkit_actiondir)/org.jaos.gslapt.policy
else
INSTALL_GSLAPT_POLKIT =
endif

EXTRA_DIST = \
  FAQ \
  gslapt.desktop.in \
  gslapt-kde.desktop.in \
  series_map.rc.in \
  slack-desc \
  slack-required \
  org.jaos.gslapt.policy.in

CLEANFILES = \
  series_map.rc \
  gslapt.desktop \
  gslapt-kde.desktop \
  org.jaos.gslapt.policy


all-local:
	grep "=" $(top_srcdir)/series_map.rc.in | sed "s/_\(.*\)=\(.*\)/\1=\"\2\"/" > $(top_builddir)/series_map.rc_quoted
	sed "s/\_//" $(top_srcdir)/series_map.rc.in > $(top_builddir)/series_map.rc
	mkdir $(top_builddir)/series_map
	cd $(top_builddir)/series_map && \
		intltool-merge --quoted-style -m -u $(top_srcdir)/po $(abs_top_builddir)/series_map.rc_quoted series_map.rc && \
		for i in $$(find . -mindepth 1 -maxdepth 1 -type d -exec basename {} \;);do \
			sed -i "s/\(.*\)=\(.*\)/\1[$$i]=\2/" $$i/series_map.rc && \
			sed "s/\"//g" $$i/series_map.rc >> $(top_builddir)/series_map.rc; \
		done
	rm -rf $(top_builddir)/series_map.rc_quoted $(top_builddir)/series_map
	sed -i -re 's,XXXDOTXXX,.,g' $(top_builddir)/series_map.rc
	sed -i -re 's,XXXSLASHXXX,/,g' $(top_builddir)/series_map.rc
	intltool-merge po/ -d -u $(top_srcdir)/gslapt.desktop.in $(top_builddir)/gslapt.desktop
	intltool-merge po/ -d -u $(top_srcdir)/gslapt-kde.desktop.in $(top_builddir)/gslapt-kde.desktop
	intltool-merge po/ -x -u $(top_srcdir)/org.jaos.gslapt.policy.in $(top_builddir)/org.jaos.gslapt.policy
	sed -i -re 's,(Exec=)(.*),\1$(sbindir)/@PACKAGE@,' $(top_builddir)/gslapt.desktop $(top_builddir)/gslapt-kde.desktop
	sed -i -re 's,(Icon=).*?(/pixmaps.*),\1$(datadir)\2,' $(top_builddir)/gslapt.desktop $(top_builddir)/gslapt-kde.desktop


install-data-local:
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)/$(pkgdatadir); \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/applications; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) $(top_srcdir)/AUTHORS $(top_srcdir)/ChangeLog $(top_srcdir)/COPYING $(top_srcdir)/FAQ $(top_srcdir)/README $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) $(top_builddir)/gslapt.desktop $(DESTDIR)/$(datadir)/applications/gslapt.desktop; \
	$(INSTALL_DATA) $(top_builddir)/gslapt-kde.desktop $(DESTDIR)/$(datadir)/applications/gslapt-kde.desktop; \
	$(INSTALL_DATA) $(top_builddir)/series_map.rc $(DESTDIR)/$(pkgdatadir)/; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/pixmaps; \
	$(INSTALL_DATA) $(top_srcdir)/ui/gslapt.png $(DESTDIR)/$(datadir)/pixmaps/
	$(INSTALL_GSLAPT_POLKIT)

uninstall-local:
	-rm -rf $(DESTDIR)/$(datadir)/doc/@PACKAGE@
	-rm -f $(DESTDIR)/$(datadir)/applications/gslapt.desktop
	-rm -f $(DESTDIR)/$(datadir)/applications/gslapt-kde.desktop
	-rm -f $(DESTDIR)/$(datadir)/pixmaps/gslapt.png
	-rm -f $(top_builddir)/series_map.rc $(DESTDIR)/$(pkgdatadir)/series_map.rc
if POLKIT
	-rm -f $(DESTDIR)/$(polkit_actiondir)/org.jaos.gslapt.policy
	-rm -f $(DESTDIR)/$(bindir)/gslapt-polkit
endif

pkg: all
	@make install DESTDIR=$(PKGDIR)
	$(mkinstalldirs) $(PKGDIR)/install
	install slack-desc $(PKGDIR)/install/
	install slack-required $(PKGDIR)/install/
	-chown $$(stat --format "%u:%g" /usr/sbin) $(PKGDIR)/$(sbindir)
	-chown $$(stat --format "%u:%g" /usr/sbin) $(PKGDIR)/$(sbindir)/@PACKAGE@
	@echo -n "for DESKTOPFILE in " > $(PKGDIR)/install/doinst.sh
	@echo -n "$(DESTDIR)/$(datadir)/applications/gslapt.desktop " |sed -re "s/^\/+//" >> $(PKGDIR)/install/doinst.sh
	@echo "$(DESTDIR)/$(datadir)/applications/gslapt-kde.desktop" |sed -re "s/^\/+//" >> $(PKGDIR)/install/doinst.sh
	@echo "do" >> $(PKGDIR)/install/doinst.sh
	@echo 'if [ -f $$DESKTOPFILE ]; then for privprog in $(PKEXEC) /usr/bin/gksudo /usr/bin/gksu /usr/bin/gnomesu /usr/bin/kdesu;do if [ -x ./$$privprog ]; then shortprivprog=$$(basename $$privprog); sed -i.bak -re "s/(Exec=)(\/.*)/\1$$shortprivprog \2/" $$DESKTOPFILE; break; fi; done; fi' >> $(PKGDIR)/install/doinst.sh
	@echo "done" >> $(PKGDIR)/install/doinst.sh
	@echo 'if [ -x usr/bin/update-desktop-database ]; then usr/bin/update-desktop-database &>/dev/null; fi' >> $(PKGDIR)/install/doinst.sh
	strip --strip-unneeded $(PKGDIR)/$(sbindir)/@PACKAGE@
	( cd $(PKGDIR) && /sbin/makepkg -l y -c n ../@PACKAGE@-@VERSION@-@host_cpu@-$(PKGREL).txz )


