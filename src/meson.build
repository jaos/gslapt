
sources = [
  'main.c',
  'callbacks.c',
  'callbacks.h',
  'series.c',
  'series.h',
  'settings.c',
  'settings.h',
]

configure_file(output: 'config.h', configuration: configuration)
executable('gslapt', sources, dependencies : deps, install: true, install_dir: get_option('sbindir'), link_args: ['-Wl,--export-dynamic'])
