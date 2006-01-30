happygnomedir = $(datadir)/gdm/themes/happygnome
happygnome_DATA = \
	GdmGreeterTheme.desktop \
	happygnome.xml \
	background.svg \
	gnome-logo.png  \
	options.png \
	screenshot.png

happygnome_in_files = \
	GdmGreeterTheme.desktop.in

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(happygnome_DATA) $(happygnome_in_files)

clean-local:
	/bin/rm -f GdmGreeterTheme.desktop
