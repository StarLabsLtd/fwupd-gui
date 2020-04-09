subdir('appdata')
subdir('icons')

i18n.merge_file(
  input: 'firmware-update.desktop.in',
  output: 'firmware-update.desktop',
  type: 'desktop',
  po_dir: join_paths(meson.source_root(), 'po'),
  install: true,
  install_dir: join_paths(prefixed_datadir, 'applications'),
)
