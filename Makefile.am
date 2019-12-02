## Process this file with automake to produce Makefile.in

SUBDIRS = src po ui

PKGDIR = `pwd`/pkg
PKGREL = 1
if POLKIT
INSTALL_GSLAPT_POLKIT = mkdir -p $(DESTDIR)/$(bindir); echo -e "\#!/bin/sh\npkexec --disable-internal-agent $(sbindir)/gslapt \$$@" > $(DESTDIR)/$(bindir)/gslapt-polkit; chmod +x $(DESTDIR)/$(bindir)/gslapt-polkit; $(mkinstalldirs) $(DESTDIR)/$(POLKIT_SYSTEM_ACTIONS_DIR); $(INSTALL_DATA) org.jaos.gslapt.policy $(DESTDIR)/$(POLKIT_SYSTEM_ACTIONS_DIR)/org.jaos.gslapt.policy; sed -i -re 's,(exec\.path">).*(<\/annotate>),\1$(sbindir)/@PACKAGE@\2,g' $(DESTDIR)/$(POLKIT_SYSTEM_ACTIONS_DIR)/org.jaos.gslapt.policy
else
INSTALL_GSLAPT_POLKIT =
endif

EXTRA_DIST = \
  FAQ \
  gslapt.desktop.in \
  gslapt-kde.desktop.in \
  series_map.rc.in \
  slack-desc \
  slack-required

all-local:
	@grep "=" series_map.rc.in | sed "s/_\(.*\)=\(.*\)/\1=\"\2\"/" > series_map.rc_quoted
	@sed "s/\_//" series_map.rc.in > series_map.rc
	@mkdir series_map
	@cd series_map && \
		intltool-merge --quoted-style -m -u ../po ../series_map.rc_quoted series_map.rc && \
		for i in $$(find . -mindepth 1 -maxdepth 1 -type d -exec basename {} \;);do \
			sed -i "s/\(.*\)=\(.*\)/\1[$$i]=\2/" $$i/series_map.rc && \
			sed "s/\"//g" $$i/series_map.rc >> ../series_map.rc; \
		done
	@rm -rf series_map.rc_quoted series_map
	@intltool-merge po/ -d -u gslapt.desktop.in gslapt.desktop
	@intltool-merge po/ -d -u gslapt-kde.desktop.in gslapt-kde.desktop


install-data-local:
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)/$(pkgdatadir); \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/applications; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) AUTHORS ChangeLog COPYING FAQ README $(DESTDIR)/$(datadir)/doc/@PACKAGE@ ; \
	$(INSTALL_DATA) gslapt.desktop $(DESTDIR)/$(datadir)/applications/gslapt.desktop; \
	$(INSTALL_DATA) gslapt-kde.desktop $(DESTDIR)/$(datadir)/applications/gslapt-kde.desktop; \
	$(INSTALL_DATA) series_map.rc $(DESTDIR)/$(pkgdatadir)/; \
	$(mkinstalldirs) $(DESTDIR)/$(datadir)/pixmaps; \
	$(INSTALL_DATA) ui/gslapt.png $(DESTDIR)/$(datadir)/pixmaps/
	$(INSTALL_GSLAPT_POLKIT)

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
	@echo 'if [ -f $$DESKTOPFILE ]; then for privprog in usr/bin/pkexec usr/bin/gksudo usr/bin/gksu usr/bin/gnomesu usr/bin/kdesu;do if [ -x $$privprog ]; then shortprivprog=$$(basename $$privprog); sed -i.bak -re "s/(Exec=)(\/usr\/sbin\/gslapt)/\1$$shortprivprog \2/" $$DESKTOPFILE; fi; done; fi' >> $(PKGDIR)/install/doinst.sh
	@echo "done" >> $(PKGDIR)/install/doinst.sh
	@echo 'if [ -x usr/bin/update-desktop-database ]; then usr/bin/update-desktop-database &>/dev/null; fi' >> $(PKGDIR)/install/doinst.sh
	strip --strip-unneeded $(PKGDIR)/$(sbindir)/@PACKAGE@
	( cd $(PKGDIR) && /sbin/makepkg -l y -c n ../@PACKAGE@-@VERSION@-@host_cpu@-$(PKGREL).txz )


