#i18n.merge_file(
#	input: 'firmware-update.metainfo.xml.in',
#	output: 'firmware-update.metainfo.xml',
#	type: 'xml',
#	po_dir: join_paths(meson.source_root(), 'po'),
#	install: true,
#	install_dir: join_paths(prefixed_datadir, 'metainfo'),
#)

made_xml = [
	'firmware-update.metainfo.xml',
]
install_data(made_xml, install_dir: join_paths(prefixed_datadir, 'metainfo'))




#install_data(
#	'org.freedesktop.fwupd.rules',
#	install_dir : 'share/polkit-1/rules.d')

#envbin = find_program('env')

#gettext_data_dir = 'GETTEXTDATADIRS=' + meson.source_root()

#custom_target('firmware-update.metainfo',
#	output: 'firmware-update.metainfo.xml',
#	input: 'firmware-update.metainfo.xml.in',
#	command: [envbin, gettext_data_dir, 'msgfmt', '--xml',
#		'--template', '@INPUT@',
#		'-d', join_paths(meson.source_root(), '/po'),
#		'-o', '@OUTPUT@'],
#	install: true,
#	install_dir: join_paths(prefixed_datadir, 'metainfo')
#)
