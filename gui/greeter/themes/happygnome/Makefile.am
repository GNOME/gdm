happygnomedir = $(datadir)/gdm/themes/happygnome
happygnome_DATA = \
	GdmGreeterTheme.desktop \
	happygnome.xml \
	background.svg \
	gnome-logo.png  \
	options.png \
	screenshot.png

happygnome_in_files = \
	GdmGreeterTheme.desktop.in \
	happygnome.xml.in

@INTLTOOL_XML_RULE@
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(happygnome_DATA) $(happygnome_in_files)
