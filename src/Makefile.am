pkglibexec_PROGRAMS = \
	gooroom-session-manager \
	grac-reload-helper \
	gooroom-update-blacklist-helper

gooroom_session_manager_SOURCES = \
	panel-glib.c \
	gooroom-session-manager.c

gooroom_session_manager_CFLAGS = \
	-DGNOMELOCALEDIR=\"$(localedir)\"	\
	-DGRAC_RELOAD_HELPER=\"$(pkglibexecdir)/grac-reload-helper\"  \
	-DGOOROOM_UPDATE_BLACKLIST_HELPER=\"$(pkglibexecdir)/gooroom-update-blacklist-helper\"  \
	$(GTK_CFLAGS) 		\
	$(GLIB_CFLAGS) 	\
	$(JSON_C_CFLAGS)	\
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

grac_reload_helper_SOURCES = \
	grac-reload-helper.c

grac_reload_helper_CFLAGS = \
	$(GIO_CFLAGS)

grac_reload_helper_LDFLAGS = \
	$(GIO_LIBS)

gooroom_update_blacklist_helper_SOURCES = \
	panel-glib.c \
	gooroom-update-blacklist-helper.c

gooroom_update_blacklist_helper_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)

gooroom_update_blacklist_helper_LDFLAGS = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)
