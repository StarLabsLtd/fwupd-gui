cargs = ['-DG_LOG_DOMAIN="Gfu"']

gnome_firmware_resources = gnome.compile_resources(
  'gnome-firmware-resources',
  'gnome-firmware.gresource.xml',
  source_dir : '.',
  c_name : 'gfu',
)

gnome_firmware = executable(
  'gnome-firmware',
  gnome_firmware_resources,
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
  custom_target('gnome-firmware-man',
    input : gnome_firmware,
    output : 'gnome-firmware.1',
    command : [
      help2man, '@INPUT@',
      '--no-info',
      '--output', '@OUTPUT@',
      '--name', 'gnome-firmware',
      '--manual', 'User Commands',
      '--version-string', meson.project_version(),
    ],
    install : true,
    install_dir : join_paths(prefixed_mandir, 'man1'),
  )
endif
