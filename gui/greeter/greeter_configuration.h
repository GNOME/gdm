#ifndef GREETER_CONFIGURATION_H
#define GREETER_CONFIGURATION_H

extern gboolean GdmUseCirclesInEntry;
#if 0
/* FIXME: maybe just whack this */
extern gboolean GdmShowGnomeChooserSession;
#endif
extern gboolean GdmShowGnomeFailsafeSession;
extern gboolean GdmShowXtermFailsafeSession;
extern gboolean GdmShowLastSession;
extern gboolean GdmSystemMenu;
extern gboolean GdmConfigAvailable;
extern gboolean GdmChooserButton;
extern gchar *GdmHalt;
extern gchar *GdmReboot;
extern gchar *GdmSuspend;
extern gchar *GdmConfigurator;
extern gchar *GdmSessionDir;
extern gchar *GdmDefaultSession;
extern gchar *GdmDefaultLocale;
extern gchar *GdmLocaleFile;
extern gboolean GdmTimedLoginEnable;
extern gboolean GdmUse24Clock;
extern gchar *GdmTimedLogin;
extern gint GdmTimedLoginDelay;
extern gchar *GdmGlobalFaceDir;
extern gchar *GdmDefaultFace;
extern gint  GdmIconMaxHeight;
extern gint  GdmIconMaxWidth;
extern gchar *GdmExclude;
extern int GdmMinimalUID;
extern gboolean GdmAllowRoot;
extern gboolean GdmAllowRemoteRoot;
extern gchar *GdmWelcome;

extern gboolean GDM_IS_LOCAL;
extern gboolean DOING_GDM_DEVELOPMENT;

#endif /* GREETER_CONFIGURATION_H */
