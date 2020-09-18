pkglibexec_PROGRAMS = \
	gooroom-session-manager \
	grac-reload-helper \
	gooroom-update-blacklist-helper

BUILT_SOURCES = \
	gsm-resources.c \
	gsm-resources.h

gooroom_session_manager_SOURCES = \
	$(BUILT_SOURCES) \
	gsm-message-dialog.h \
	gsm-message-dialog.c \
	gs-grab.h \
	gs-grab-x11.c \
	panel-glib.c \
	gooroom-session-manager.c

gooroom_session_manager_CFLAGS = \
	-DGNOMELOCALEDIR=\"$(localedir)\"	\
	-DGRAC_RELOAD_HELPER=\"$(pkglibexecdir)/grac-reload-helper\"  \
	-DGOOROOM_UPDATE_BLACKLIST_HELPER=\"$(pkglibexecdir)/gooroom-update-blacklist-helper\"  \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(JSON_C_CFLAGS) \
	$(DBUS_GLIB_CFLAGS) \
	$(LIBNOTIFY_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(GNOME_DESKTOP_CFLAGS) \
	$(X11_CFLAGS)

gooroom_session_manager_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(JSON_C_LIBS) \
	$(DBUS_GLIB_LIBS) \
	$(LIBNOTIFY_LIBS) \
	$(POLKIT_LIBS) \
	$(GNOME_DESKTOP_LIBS) \
	$(X11_LIBS)

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gresource.xml)
gsm-resources.c: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name gsm $<
gsm-resources.h: gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name gsm $<

CLEANFILES = $(BUILT_SOURCES)


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
