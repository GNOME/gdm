circlesdir = $(datadir)/gdm/themes/circles
circles_DATA = \
	GdmGreeterTheme.desktop \
	happygnome.xml \
	background.svg \
	gnome-logo.png  \
	options.png \
	screenshot.png

circles_in_files = \
	GdmGreeterTheme.desktop.in \
	circles.xml.in

@INTLTOOL_XML_RULE@
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(circles_DATA) $(circles_in_files)
