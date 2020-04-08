project('gnome-firmware', 'c',
  version : '3.34.1',
  default_options : ['warning_level=1'],
  meson_version : '>=0.46.0'
)

conf = configuration_data()
conf.set('PACKAGE_VERSION', meson.project_version())

# get suported warning flags
test_args = [
  '-Waggregate-return',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wextra',
  '-Wformat=2',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Winline',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wno-cast-function-type',
  '-Wold-style-definition',
  '-Wno-format-nonliteral',
  '-Woverride-init',
  '-Wpacked',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
foreach arg: test_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
foreach arg: test_link_args
  if cc.has_link_argument(arg)
    add_project_link_arguments(arg, language : 'c')
  endif
endforeach

prefixed_localedir = join_paths(get_option('prefix'), get_option('localedir'))
prefixed_datadir = join_paths(get_option('prefix'), get_option('datadir'))
prefixed_mandir = join_paths(get_option('prefix'), get_option('mandir'))

libgtk = dependency('gtk+-3.0', version : '>= 3.11.2')
libgio = dependency('gio-2.0')
libfwupd = dependency('fwupd', version : '>= 1.2.10')
libxmlb = dependency('xmlb', version : '>=0.1.7', fallback : ['libxmlb', 'libxmlb_dep'])
libsoup = dependency('libsoup-2.4', version : '>= 2.51.92')

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('LOCALEDIR', prefixed_localedir)

if get_option('systemd')
  systemd = dependency('systemd', version : '>= 211')
  conf.set('HAVE_SYSTEMD' , '1')
  conf.set('HAVE_LOGIND' , '1')
endif

if get_option('elogind')
  elogind = dependency('libelogind', version : '>= 211')
  conf.set('HAVE_LOGIND' , '1')
endif

if get_option('consolekit')
  conf.set('HAVE_CONSOLEKIT' , '1')
endif

configure_file(
  output : 'config.h',
  configuration : conf,
)

subdir('src')
# subdir('po')
subdir('data')

if meson.version().version_compare('<0.41.0')
  archiver = find_program('git', required : false)
  if archiver.found()
    run_target('dist',
      # git config tar.tar.xz.command "xz -c"
      command: [
        'git', 'archive',
        '--prefix=' + meson.project_name() + '-' + meson.project_version() + '/',
        'HEAD',
        '--format=tar.xz',
        '--output',
        meson.project_name() + '-' + meson.project_version() + '.tar.xz'
      ]
    )
  else
    message('git not found, you will not be able to run `ninja dist`')
  endif
endif


# FIXME: remove when https://github.com/mesonbuild/meson/issues/837 fixed
# meson.add_install_script('meson_post_install.sh')
