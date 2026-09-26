# updating
# meson compile -C build gslapt-pot
# meson compile -C build gslapt-update-po
i18n.gettext(package,
  args: [
    '--directory=' + meson.project_build_root(),
    '--keyword=P_:1,2',
    '--from-code=UTF-8',
    '--no-location',
  ])
