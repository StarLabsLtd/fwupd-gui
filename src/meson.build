cargs = ['-DG_LOG_DOMAIN="Gfu"']

firmware_update_resources = gnome.compile_resources(
  'firmware-update-resources',
  'firmware-update.gresource.xml',
  source_dir : '.',
  c_name : 'gfu',
)

firmware_update = executable(
  'firmware-update',
  firmware_update_resources,
  sources : [
    'gfu-main.c',
    'gfu-device-row.c',
    'gfu-release-row.c',
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    libgtk,
    libfwupd,
    libxmlb,
    libsoup,
  ],
  c_args : cargs,
  install : true,
)

if get_option('man')
  help2man = find_program('help2man')
  custom_target('firmware-update-man',
    input : firmware_update,
    output : 'firmware-update.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'firmware-update',
      '--manual', 'User Commands',
      '--version-string', meson.project_version(),
    ],
    install : true,
    install_dir : join_paths(prefixed_mandir, 'man1'),
  )
endif
