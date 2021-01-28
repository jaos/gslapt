ui_files = [
  'about.ui',
  'changelog_window.ui',
  'dep_error_dialog.ui',
  'dl_progress_window.ui',
  'gslapt.ui',
  'icon_legend.ui',
  'notification.ui',
  'pkgtools_progress_window.ui',
  'repositories_changed.ui',
  'source_failed_dialog.ui',
  'source_window.ui',
  'transaction_window.ui',
  'window_preferences.ui',
]
foreach ui_file: ui_files
  install_data(ui_file, install_dir: join_paths(datadir, meson.project_name(), 'ui'))
endforeach

img_files = [
  'gslapt.png',
  'pkg_action_available_excluded.png',
  'pkg_action_available.png',
  'pkg_action_downgrade.png',
  'pkg_action_installed_excluded.png',
  'pkg_action_installed.png',
  'pkg_action_install.png',
  'pkg_action_reinstall.png',
  'pkg_action_remove.png',
  'pkg_action_upgrade.png',
]

foreach img_file: img_files
  install_data(img_file, install_dir: join_paths(datadir, meson.project_name(), 'ui'))
endforeach
