happygnomedir = $(datadir)/gdm/themes/happygnome
happygnome_DATA = \
	GdmGreeterTheme.desktop \
	happygnome.xml \
	background.svg \
	disconnect.png \
	gnome-logo.png  \
	options.png \
	screenshot.png \
	session.png \
	system.png

happygnome_in_files = \
	GdmGreeterTheme.desktop.in \
	happygnome.xml.in

@INTLTOOL_XML_RULE@
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(happygnome_DATA) $(happygnome_in_files)
