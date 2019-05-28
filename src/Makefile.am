pkglibexec_PROGRAMS = gooroom-session-manager kill-gooroom-splash

gooroom_session_manager_SOURCES = \
	gooroom-session-manager.c

gooroom_session_manager_CFLAGS = \
	-DGNOMELOCALEDIR=\"$(localedir)\"	\
	-DKILL_GOOROOM_SPLASH=\"$(pkglibexecdir)/kill-gooroom-splash\"  \
	$(GTK_CFLAGS) 		\
	$(GLIB_CFLAGS) 	\
    $(JSON_C_CFLAGS)    \
    $(DBUS_GLIB_CFLAGS) \
    $(LIBNOTIFY_CFLAGS) \
    $(POLKIT_CFLAGS)

gooroom_session_manager_LDADD = \
	$(GTK_LIBS) 	\
	$(GLIB_LIBS)	\
	$(JSON_C_LIBS)  \
	$(DBUS_GLIB_LIBS)   \
	$(LIBNOTIFY_LIBS)   \
	$(POLKIT_LIBS)

gooroom_session_manager_LDFLAGS = \
	-export-dynamic

kill_gooroom_splash_SOURCES = \
	kill-gooroom-splash.c

kill_gooroom_splash_CFLAGS = \
	$(GLIB_CFLAGS)

kill_gooroom_splash_LDADD = \
	$(GLIB_LIBS)

install-exec-hook:
	chown root "$(DESTDIR)/$(pkglibexecdir)/kill-gooroom-splash"
	chmod +s "$(DESTDIR)/$(pkglibexecdir)/kill-gooroom-splash"
