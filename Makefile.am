## Process this file with automake to produce Makefile.in

SUBDIRS = src po ui

PKGDIR = `pwd`/pkg
PKGREL = 1
if POLKIT
polkitactiondir = @polkitactiondir@
INSTALL_GSLAPT_POLKIT = mkdir -p $(DESTDIR)/$(bindir); echo -e "\#!/bin/sh\npkexec --disable-internal-agent $(sbindir)/gslapt \$$@" > $(DESTDIR)/$(bindir)/gslapt-polkit; chmod +x $(DESTDIR)/$(bindir)/gslapt-polkit; $(mkinstalldirs) $(DESTDIR)/$(polkitactiondir); $(INSTALL_DATA) org.jaos.gslapt.policy $(DESTDIR)/$(polkitactiondir)/org.jaos.gslapt.policy; sed -i -re 's,[@]sbindir[@],$(sbindir),g' $(DESTDIR)/$(polkitactiondir)/org.jaos.gslapt.policy
else
INSTALL_GSLAPT_POLKIT =
endif

AM_DISTCHECK_CONFIGURE_FLAGS = \
  --with-polkitactiondir='$$(prefix)/$(polkitactiondir)'

EXTRA_DIST = \
  FAQ \
  gslapt.desktop.in \
  gslapt-kde.desktop.in \
  series_map.rc.in \
  slack-desc \
  slack-required \
  org.jaos.gslapt.policy.in \
  src/meson.build \
  ui/meson.build \
  meson.build \
  po/meson.build \
  seriesmerge.sh


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
		intltool-merge --quoted-style -m -u $(abs_top_srcdir)/po $(abs_top_builddir)/series_map.rc_quoted series_map.rc && \
		for i in $$(find . -mindepth 1 -maxdepth 1 -type d -exec basename {} \;);do \
			sed -i "s/\(.*\)=\(.*\)/\1[$$i]=\2/" $$i/series_map.rc && \
			sed "s/\"//g" $$i/series_map.rc >> $(abs_top_builddir)/series_map.rc; \
		done
	rm -rf $(top_builddir)/series_map.rc_quoted $(top_builddir)/series_map
	sed -i -r -e 's,XXXDOTXXX,.,g' -e 's,XXXSLASHXXX,/,g' $(top_builddir)/series_map.rc
	msgfmt --desktop -d po/ --template $(top_srcdir)/gslapt.desktop.in -o $(top_builddir)/gslapt.desktop
	msgfmt --desktop -d po/ --template $(top_srcdir)/gslapt-kde.desktop.in -o $(top_builddir)/gslapt-kde.desktop
	intltool-merge po/ -x -u $(top_srcdir)/org.jaos.gslapt.policy.in $(top_builddir)/org.jaos.gslapt.policy
	sed -i -r -e 's,[@]sbindir[@],$(sbindir),g' -e 's,[@]datadir[@],$(datadir),g' -e "s,[@]privprog[@],$(PKEXEC) ," $(top_builddir)/gslapt.desktop $(top_builddir)/gslapt-kde.desktop


install-data-local: install-exec
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
	-rm -f $(DESTDIR)/$(polkitactiondir)/org.jaos.gslapt.policy
	-rm -f $(DESTDIR)/$(bindir)/gslapt-polkit
endif

pkg: all
	@make install DESTDIR=$(PKGDIR)
	$(mkinstalldirs) $(PKGDIR)/install
	install slack-desc $(PKGDIR)/install/
	install slack-required $(PKGDIR)/install/
	chown $$(stat --format "%u:%g" /usr/sbin) $(PKGDIR)/$(sbindir)
	chown $$(stat --format "%u:%g" /usr/sbin) $(PKGDIR)/$(sbindir)/@PACKAGE@
	@echo 'if [ -x usr/bin/update-desktop-database ]; then usr/bin/update-desktop-database &>/dev/null; fi' >> $(PKGDIR)/install/doinst.sh
	strip --strip-unneeded $(PKGDIR)/$(sbindir)/@PACKAGE@
	( cd $(PKGDIR) && /sbin/makepkg -l y -c n ../@PACKAGE@-@VERSION@-@host_cpu@-$(PKGREL).txz )
