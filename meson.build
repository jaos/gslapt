project('gslapt', 'c',
  version: '0.5.7',
  license: 'GPLv2',
  meson_version: '>= 0.53',
  default_options: [
    'buildtype=debugoptimized',
    'b_lto=true',
    'warning_level=3',
  ],
)
package = meson.project_name()
version = meson.project_version()
i18n = import('i18n')

# paths
prefix = get_option('prefix')
localedir = join_paths(prefix, get_option('localedir'))
datadir = join_paths(prefix, get_option('datadir'))
pkgdatadir = join_paths(datadir, package)
pixmapsdir = join_paths(datadir, 'pixmaps')
polkitactiondir = join_paths(datadir, 'polkit-1', 'actions') # default, let pkg-config tell us the truth
docdatadir = join_paths(datadir, 'doc', package)
sbindir = join_paths(get_option('prefix'), get_option('sbindir'))

configuration = configuration_data()
configuration.set_quoted('PACKAGE', package)
configuration.set_quoted('GETTEXT_PACKAGE', package)
configuration.set_quoted('PACKAGE_DATA_DIR', datadir)
configuration.set_quoted('PACKAGE_LOCALE_DIR', localedir)
configuration.set_quoted('VERSION', version)
configuration.set('sbindir', sbindir)
configuration.set('datadir', datadir)
configuration.set('privprog', '')

# compiler based checks
libcurl = dependency('libcurl')
zlib = dependency('zlib')
openssl = dependency('openssl')
libgpgme = dependency('gpgme', required: false)
cc = meson.get_compiler('c')
libm = cc.find_library('m')
# libslapt = dependency('libslapt', version: '>=0.11.1') # use this when we have a few pkg-config enabled releases out
libslapt = cc.find_library('slapt', has_headers: ['slapt.h'])
if libslapt.found()
  # if libgpgme.found() and cc.has_function('slapt_add_pkg_source_gpg_key', dependencies: [libslapt])
  if libgpgme.found() and cc.has_function('slapt_add_pkg_source_gpg_key', dependencies: [libcurl, zlib, openssl, libgpgme, libslapt])
    configuration.set('SLAPT_HAS_GPGME', 1)
  endif
endif

# polkit
polkit_dep = dependency('polkit-gobject-1', required: false)
if polkit_dep.found()
  polkitactiondir = polkit_dep.get_pkgconfig_variable('actiondir')#, define_variable: ['prefix', prefix])
endif
pkexec = find_program('pkexec', required: false)
if pkexec.found()
  configuration.set('privprog', pkexec.path() + ' ')
  custom_target('org.jaos.gslapt.policy',
    input: configure_file(output: 'org.jaos.gslapt.policy.pre', input: 'org.jaos.gslapt.policy.in', configuration: configuration),
    output: 'org.jaos.gslapt.policy',
    command: ['intltool-merge', join_paths(meson.source_root(), 'po'), '-x', '-u', '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: polkitactiondir,
  )
  custom_target('gslapt-polkit',
    output: 'gslapt-polkit',
    command: [find_program('mkpolkitwrapper.sh'), sbindir, '@OUTPUT@'],
    install: true,
    install_dir: get_option('bindir'),
  )
endif

deps = [libcurl, zlib, openssl, libm, libgpgme, libslapt,
  dependency('glib-2.0'),
  dependency('gtk+-3.0', version: '>=3.4.0'),
]

i18n.merge_file(
  input: configure_file(output: 'gslapt.desktop.pre', input: 'gslapt.desktop.in', configuration: configuration),
  output: 'gslapt.desktop',
  type: 'desktop',
  po_dir: 'po',
  install: true,
  install_dir: get_option('datadir') / 'applications'
)
i18n.merge_file(
  input: configure_file(output: 'gslapt-kde.desktop.pre', input: 'gslapt-kde.desktop.in', configuration: configuration),
  output: 'gslapt-kde.desktop',
  type: 'desktop',
  po_dir: 'po',
  install: true,
  install_dir: get_option('datadir') / 'applications'
)
install_data('ui/gslapt.png', install_dir: pixmapsdir)
install_data(['AUTHORS', 'ChangeLog', 'COPYING', 'FAQ', 'README'], install_dir: docdatadir)
custom_target('series_map.rc',
  input: 'series_map.rc.in',
  output: 'series_map.rc',
  command: [find_program('seriesmerge.sh'), '@INPUT@', '@OUTPUT@', join_paths(meson.source_root())],
  install: true,
  install_dir: pkgdatadir,
)

subdir('po')
subdir('src')
subdir('ui')
