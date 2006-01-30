circlesdir = $(datadir)/gdm/themes/circles
circles_DATA = \
	GdmGreeterTheme.desktop \
	circles.xml \
	background.svg \
	flower.png  \
	help.png \
	options.png \
	screenshot.png

circles_in_files = \
	GdmGreeterTheme.desktop.in

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(circles_DATA) $(circles_in_files)

clean-local:
	/bin/rm -f GdmGreeterTheme.desktop
