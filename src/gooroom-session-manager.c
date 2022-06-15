/*
 *  Copyright (c) 2015-2019 Gooroom <gooroom@gooroom.kr>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <libintl.h>
#include <pwd.h>
#include <sys/stat.h>

#include <dbus/dbus.h>
#include <json-c/json.h>
#include <polkit/polkit.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>

#include <libnotify/notify.h>

#include "panel-glib.h"

#define	GRM_USER		        ".grm-user"
#define	BACKGROUND_PATH         "/usr/share/backgrounds/gooroom/"
#define	DEFAULT_BACKGROUND      "/usr/share/images/desktop-base/desktop-background.xml"
#define GCSR_CONF               "/etc/gooroom/gooroom-client-server-register/gcsr.conf"


static GSettings  *g_blacklist_settings = NULL;
static GSettings  *g_whitelist_settings = NULL;
static GDBusProxy *g_grac_proxy = NULL;
static GDBusProxy *g_agent_proxy = NULL;
static guint g_gda_watch_id = 0;
static guint g_owner_id = 0, g_timeout_id = 0;
static guint g_agent_signal_id = 0, g_grac_signal_id = 0;
static gboolean g_agent_name_appeared = FALSE;

static void gooroom_blacklist_settings_changed (GSettings *settings, const gchar *key, gpointer data);



static void
show_notification (const gchar *summary, const gchar *message, const gchar *icon)
{
	NotifyNotification *notification;

	if (!summary)
		summary = _("Notification");

	notify_init (PACKAGE_NAME);
	notification = notify_notification_new (summary, message, icon);

	notify_notification_set_urgency (notification, NOTIFY_URGENCY_NORMAL);
	notify_notification_set_timeout (notification, NOTIFY_EXPIRES_DEFAULT);
	notify_notification_show (notification, NULL);

	g_object_unref (notification);
}

static gboolean
is_gpms_user (const gchar *username)
{
	gboolean ret = FALSE;

	struct passwd *entry = getpwnam (username);
	if (entry) {
		gchar **tokens = g_strsplit (entry->pw_gecos, ",", -1);
		if (g_strv_length (tokens) > 4 ) {
			if (tokens[4] && (g_strcmp0 (tokens[4], "gooroom-account") == 0)) {
				ret = TRUE;
			}
		}
		g_strfreev (tokens);
	}

	return ret;
}

static gboolean
get_object_path (gchar **object_path, const gchar *service_name)
{
	GVariant   *variant;
	GDBusProxy *proxy;
	GError     *error = NULL;

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
			G_DBUS_CALL_FLAGS_NONE, NULL,
			"org.freedesktop.systemd1",
			"/org/freedesktop/systemd1",
			"org.freedesktop.systemd1.Manager",
			NULL, &error);

	if (!proxy) {
		g_error_free (error);
		return FALSE;
	}

	variant = g_dbus_proxy_call_sync (proxy, "GetUnit",
			g_variant_new ("(s)", service_name),
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	if (!variant) {
		g_error_free (error);
	} else {
		g_variant_get (variant, "(o)", object_path);
		g_variant_unref (variant);
	}

	g_object_unref (proxy);

	return TRUE;
}


static gboolean
is_systemd_service_active (const gchar *service_name)
{
	gboolean ret = FALSE;

	GVariant   *variant;
	GDBusProxy *proxy;
	GError     *error = NULL;
	gchar      *obj_path = NULL;

	get_object_path (&obj_path, service_name);
	if (!obj_path) {
		goto done;
	}

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
			G_DBUS_CALL_FLAGS_NONE, NULL,
			"org.freedesktop.systemd1",
			obj_path,
			"org.freedesktop.DBus.Properties",
			NULL, &error);

	if (!proxy)
		goto done;

	variant = g_dbus_proxy_call_sync (proxy, "GetAll",
			g_variant_new ("(s)", "org.freedesktop.systemd1.Unit"),
			G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	if (variant) {
		gchar *output = NULL;
		GVariant *asv = g_variant_get_child_value(variant, 0);
		GVariant *value = g_variant_lookup_value(asv, "ActiveState", NULL);
		if(value && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
			output = g_variant_dup_string(value, NULL);
			if (g_strcmp0 (output, "active") == 0) {
				ret = TRUE;;
			}
			g_free (output);
		}

		g_variant_unref (variant);
	}

	g_object_unref (proxy);

done:
	if (error)
		g_error_free (error);

	return ret;
}

static gboolean
registered_gpms (void)
{
	gchar *glm = NULL;
	gboolean ret = FALSE;
	GKeyFile *keyfile = NULL;

	keyfile = g_key_file_new ();
	if (g_key_file_load_from_file (keyfile, GCSR_CONF, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
		glm  = g_key_file_get_string (keyfile, "domain", "glm", NULL);
	}

	ret = (glm != NULL) ? TRUE : FALSE;

	g_free (glm);
	g_key_file_free (keyfile);

	return ret;
}


json_object *
JSON_OBJECT_GET (json_object *root_obj, const char *key)
{
	if (!root_obj) return NULL;

	json_object *ret_obj = NULL;

	json_object_object_get_ex (root_obj, key, &ret_obj);

	return ret_obj;
}

static gchar *
get_grm_user_data (void)
{
	gchar *data, *file = NULL;

	file = g_strdup_printf ("%s/.gooroom/%s", g_get_home_dir (), GRM_USER);

	if (!g_file_test (file, G_FILE_TEST_EXISTS))
		goto error;
	
	g_file_get_contents (file, &data, NULL, NULL);

error:
	g_free (file);

	return data;
}

static void
dpms_off_time_update (gint32 value)
{
	gint32 val;
	GSettings *settings;

	val = (value < 0) ? 0 : value * 60;
	val = (value * 60 > G_MAXUINT) ? G_MAXUINT : value * 60;

	settings = g_settings_new ("org.gnome.desktop.session");
	g_settings_set_uint (settings, "idle-delay", val);
	g_object_unref (settings);
}

static void
sleep_inactive_time_update (gint32 value)
{
	gint32 val;
	GSettings *settings;

	val = (value < 0) ? 0 : value * 60;
	val = (value * 60 > G_MAXUINT) ? G_MAXUINT : value * 60;

	settings = g_settings_new ("org.gnome.settings-daemon.plugins.power");
	g_settings_set_int (settings, "sleep-inactive-ac-timeout", val);
	g_settings_set_int (settings, "sleep-inactive-battery-timeout", val);
	/* blank = 0 suspend = 1 shutdown = 2 hibernate = 3 interactive = 4 nothing = 5 logout = 6 */
	g_settings_set_enum (settings, "sleep-inactive-ac-type", 1);
	g_settings_set_enum (settings, "sleep-inactive-battery-type", 1);

	g_object_unref (settings);
}

static GDBusProxy *
proxy_get (const char *id)
{
	GDBusProxy *proxy = NULL;

	if (g_str_equal (id, "agent")) {
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
				G_DBUS_CALL_FLAGS_NONE,
				NULL,
				"kr.gooroom.agent",
				"/kr/gooroom/agent",
				"kr.gooroom.agent",
				NULL,
				NULL);
	} else if (g_str_equal (id, "grac")) {
		proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
				G_DBUS_CALL_FLAGS_NONE,
				NULL,
				"kr.gooroom.GRACDEVD",
				"/kr/gooroom/GRACDEVD",
				"kr.gooroom.GRACDEVD",
				NULL,
				NULL);
	} 

	return proxy;
}

static gchar *
get_dpms_off_time_from_json (const gchar *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	gchar *ret = NULL;

	enum json_tokener_error jerr = json_tokener_success;
	json_object *root_obj = json_tokener_parse_verbose (data, &jerr);

	if (jerr == json_tokener_success) {
		json_object *obj1 = NULL, *obj2 = NULL, *obj3 = NULL, *obj4 = NULL;
		obj1 = JSON_OBJECT_GET (root_obj, "module");
		obj2 = JSON_OBJECT_GET (obj1, "task");
		obj3 = JSON_OBJECT_GET (obj2, "out");
		obj4 = JSON_OBJECT_GET (obj3, "status");
		if (obj4) {
			const char *val = json_object_get_string (obj4);
			if (val && g_strcmp0 (val, "200") == 0) {
				json_object *obj = JSON_OBJECT_GET (obj3, "screen_time");
				ret = g_strdup (json_object_get_string (obj));
			}
		}
		json_object_put (root_obj);
	}

	return ret;
}

static gchar *
get_sleep_inactive_time_from_json (const gchar *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	gchar *ret = NULL;

	enum json_tokener_error jerr = json_tokener_success;
	json_object *root_obj = json_tokener_parse_verbose (data, &jerr);

	if (jerr == json_tokener_success) {
		json_object *obj1 = NULL, *obj2 = NULL, *obj3 = NULL, *obj4 = NULL;
		obj1 = JSON_OBJECT_GET (root_obj, "module");
		obj2 = JSON_OBJECT_GET (obj1, "task");
		obj3 = JSON_OBJECT_GET (obj2, "out");
		obj4 = JSON_OBJECT_GET (obj3, "status");
		if (obj4) {
			const char *val = json_object_get_string (obj4);
			if (val && g_strcmp0 (val, "200") == 0) {
				json_object *obj = JSON_OBJECT_GET (obj3, "sleep_inactive_time");
				ret = g_strdup (json_object_get_string (obj));
			}
		}
		json_object_put (root_obj);
	}

	return ret;
}

static void
set_theme (const gchar *theme_idx)
{
	g_return_if_fail (theme_idx != NULL);

	GSettings *settings;
	const gchar *icon_theme, *background;

	if (g_str_equal (theme_idx, "1")) {
		icon_theme = "Gooroom-Arc";
		background = BACKGROUND_PATH"gooroom_theme_bg_1.jpg";
	} else if (g_str_equal (theme_idx, "2")) {
		icon_theme = "Gooroom-Faenza";
		background = BACKGROUND_PATH"gooroom_theme_bg_2.jpg";
	} else if (g_str_equal (theme_idx, "3")) {
		icon_theme = "Gooroom-Papirus";
		background = BACKGROUND_PATH"gooroom_theme_bg_3.jpg";
	} else {
		icon_theme = g_strdup(theme_idx);
		background = g_strdup_printf ("%sgooroom_user_theme_bg_%s.jpg", BACKGROUND_PATH, theme_idx);
	}

	if (!g_file_test (background, G_FILE_TEST_EXISTS))
		background = DEFAULT_BACKGROUND;

	gchar *bg_file = g_strdup_printf ("file://%s", background);

	settings = g_settings_new ("org.gnome.desktop.interface");
	g_settings_set_string (settings, "icon-theme", icon_theme);
	g_object_unref (settings);

	settings = g_settings_new ("org.gnome.desktop.background");
	g_settings_set_string (settings, "picture-uri", bg_file);
	g_object_unref (settings);

	settings = g_settings_new ("org.gnome.desktop.screensaver");
	g_settings_set_string (settings, "picture-uri", bg_file);
	g_object_unref (settings);

	g_free (bg_file);
}

static void
handle_desktop_configuration (void)
{
	gchar *data = get_grm_user_data ();

	if (data) {
		enum json_tokener_error jerr = json_tokener_success;
		json_object *root_obj = json_tokener_parse_verbose (data, &jerr);
		if (jerr == json_tokener_success) {
			json_object *obj1 = NULL, *obj2 = NULL, *obj2_1;
			obj1 = JSON_OBJECT_GET (root_obj, "data");
			obj2 = JSON_OBJECT_GET (obj1, "desktopInfo");
			obj2_1 = JSON_OBJECT_GET (obj2, "themeId");

			if (obj2_1) {
				const char *theme_idx = json_object_get_string (obj2_1);

				/* set icon theme */
				set_theme (theme_idx);
			}

			json_object_put (root_obj);
		}
	}

	g_free (data);
}

static void
grac_reload_done_cb (GPid pid, gint status, gpointer data)
{
	g_print ("Reload Grac Service done cb!!!\n");
	g_spawn_close_pid (pid);
}

static void
reload_grac_service (void)
{
	GPid pid;
	gchar *cmd;
	gchar **argv; 
	GError *error = NULL;

	cmd = g_strdup_printf ("/usr/bin/pkexec %s", GRAC_RELOAD_HELPER);

	g_shell_parse_argv (cmd, NULL, &argv, NULL);

	if (g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL))
		g_child_watch_add (pid, (GChildWatchFunc) grac_reload_done_cb, NULL);

	g_free (cmd);
	g_strfreev (argv);
}

static void
gooroom_agent_start_done_cb (GPid pid, gint status, gpointer data)
{
	g_spawn_close_pid (pid);
}

static void
set_dpms_off_time (void)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy) {
		const gchar *json;
		gchar *arg = NULL, *data = NULL;
		GVariant *variant = NULL;

		json = "{\"module\":{\"module_name\":\"config\",\"task\":{\"task_name\":\"dpms_off_time\",\"in\":{\"login_id\":\"%s\"}}}}";

		arg = g_strdup_printf (json, g_get_user_name ());

		variant = g_dbus_proxy_call_sync (g_agent_proxy,
                                          "do_task",
                                          g_variant_new ("(s)", arg),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, NULL);

		if (variant) {
			GVariant *v = NULL;
			g_variant_get (variant, "(v)", &v);
			if (v) {
				data = g_variant_dup_string (v, NULL);
				g_variant_unref (v);
			}
			g_variant_unref (variant);
		}

		if (data) {
			gchar *value = get_dpms_off_time_from_json (data);
			if (value) {
				dpms_off_time_update (atoi (value));
			} else {
				g_warning ("Failed to get dpms_off_time from Gooroom Agent Service");
			}
			g_free (value);
			g_free (data);
		}

		g_free (arg);
	}
}

static void
set_sleep_inactive_time (void)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy) {
		const gchar *json;
		gchar *arg = NULL, *data = NULL;
		GVariant *variant = NULL;

		json = "{\"module\":{\"module_name\":\"config\",\"task\":{\"task_name\":\"sleep_inactive_time\",\"in\":{\"login_id\":\"%s\"}}}}";

		arg = g_strdup_printf (json, g_get_user_name ());

		variant = g_dbus_proxy_call_sync (g_agent_proxy,
                                          "do_task",
                                          g_variant_new ("(s)", arg),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, NULL);

		if (variant) {
			GVariant *v = NULL;
			g_variant_get (variant, "(v)", &v);
			if (v) {
				data = g_variant_dup_string (v, NULL);
				g_variant_unref (v);
			}
			g_variant_unref (variant);
		}

		if (data) {
			gchar *value = get_sleep_inactive_time_from_json (data);
			if (value) {
				sleep_inactive_time_update (atoi (value));
			} else {
				g_warning ("Failed to get sleep_inactive_time from Gooroom Agent Service");
			}
			g_free (value);
			g_free (data);
		}

		g_free (arg);
	}
}

static void
do_update_operation (gint32 value)
{
	gchar *cmdline = NULL;
	NotifyNotification *notification;

	const gchar *message = NULL;
	const gchar *icon = "software-update-available-symbolic";
	const gchar *summary = _("Update Blocking Function");

	if (value == 0) {
		message = _("Update blocking function has been disabled.");
		cmdline = g_find_program_in_path ("/usr/lib/gooroom/gooroomUpdate/gooroomUpdate.py");
	} else if (value == 1) {
		message = _("Update blocking function has been enabled.");
		gchar *cmd = g_find_program_in_path ("pkill");
		if (cmd)
			cmdline = g_strdup_printf ("%s -f '/usr/lib/gooroom/gooroomUpdate/gooroomUpdate.py'", cmd);
		g_free (cmd);
	}

	g_spawn_command_line_async (cmdline, NULL);
	g_free (cmdline);

	show_notification (summary, message, icon);
}

static void
save_settings (gchar *list, const gchar *id)
{
	g_return_if_fail (list != NULL);

	GSettings *settings = NULL;
	GSettingsSchema *schema = NULL;
	const gchar *schema_name, *key;

	if (g_str_equal (id, "black_list")) {
		key = "blacklist";
		schema_name = "apps.gooroom-applauncher-applet";
		settings = g_blacklist_settings;
	} else if (g_str_equal (id, "controlcenter_items")) {
		key = "whitelist-panels";
		schema_name = "org.gnome.ControlCenter";
		settings = g_whitelist_settings;
	} else {
		key = NULL;
		schema_name = NULL;
		settings = NULL;
	}

	if (!key || !schema_name || !settings)
		return;

	gchar **filters = g_strsplit (list, ",", -1);

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                              schema_name, TRUE);

	if (g_settings_schema_has_key (schema, key))
		g_settings_set_strv (settings, key, (const char * const *) filters);

	g_strfreev (filters);

	g_settings_schema_unref (schema);
}

static void
add_theme (gchar *data)
{
	gchar *pkexec, *cmd;

	pkexec = g_find_program_in_path ("pkexec");

	if (data) {
		cmd = g_strdup_printf ("%s %s %s", pkexec, GOOROOM_THEME_ADD_HELPER, data);
	}

	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	g_free (pkexec);
	g_free (cmd);
}

static void
delete_theme (gchar *data)
{
	gchar *pkexec, *cmd;

	pkexec = g_find_program_in_path ("pkexec");

	if (data) {
		//data is ID
		cmd = g_strdup_printf ("%s %s %s", pkexec, GOOROOM_THEME_DELETE_HELPER, data);
	}

	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	g_free (pkexec);
	g_free (cmd);
}

static void
grac_notifications_close (GList *list)
{
	GList *l = NULL;
	for (l = list; l; l = l->next) {
		NotifyNotification *n = (NotifyNotification *)l->data;
		if (n) notify_notification_close (n, NULL);
	}
}

#ifdef GRAC_DEBUG
#define GRAC_LOG(fmt, args...) g_print("[%s] " fmt, __func__, ##args)
#else
#define GRAC_LOG(fmt, args...) 
#endif //GRAC_DEBUG

static void
do_resource_access_control (gchar *data)
{
	if (!data) {
		GRAC_LOG ("data is null\n");
		return;
	}

	enum json_tokener_error jerr = json_tokener_success;
	json_object *root_obj = json_tokener_parse_verbose (data, &jerr);
	if (jerr != json_tokener_success) {
		GRAC_LOG ("json parsing error(%d)\n", jerr);
		return;
	}

	json_object *obj_title = JSON_OBJECT_GET (root_obj, "title");
	if (!obj_title) 
		goto NO_CARE;

	const char *title = json_object_get_string (obj_title);
	if (!title) 
		goto NO_CARE;
	
	if (g_strcmp0 (title, "media-control") == 0) {
		json_object *obj_body = JSON_OBJECT_GET (root_obj, "body");
		if (!obj_body) 
			goto NO_CARE;

		json_object *obj_media = JSON_OBJECT_GET (obj_body, "media");
		json_object *obj_control = JSON_OBJECT_GET (obj_body, "control");
		if (!obj_media || !obj_control) 
			goto NO_CARE;

		const char *media = json_object_get_string (obj_media);
		const char *control = json_object_get_string (obj_control);
		if (!media || !control) 
			goto NO_CARE;

		char *cmd = g_strdup_printf ("/usr/bin/grac-pactl.py %s %s", media, control);
		g_spawn_command_line_async (cmd, NULL);
		GRAC_LOG("%s => done\n", cmd);
		g_free(cmd);
	}
	else {
		goto NO_CARE;
	}

	json_object_put (root_obj);
	return;

NO_CARE:
	json_object_put (root_obj);
	GRAC_LOG ("unknown json-data=%s\n", data);
	return;
}

static void
grac_signal_cb (GDBusProxy *proxy,
                gchar *sender_name,
                gchar *signal_name,
                GVariant *parameters,
                gpointer user_data)
{
	gchar *data = NULL;
	GVariant *v = NULL;

	if (g_str_equal (signal_name, "grac_letter")) {
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			data = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
		}

		if (data) {
			do_resource_access_control (data);
			g_free (data);
		}
	} else if (g_str_equal (signal_name, "grac_noti")) {
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			data = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
		}

		if (data) {
			gchar **infos = g_strsplit (data, ":", -1);
			show_notification (_("Gooroom Resource Access Control"), infos[1], "dialog-information");
			g_strfreev (infos);
			g_free (data);
		}
	} else {
		return;
	}
}

static void
agent_signal_cb (GDBusProxy *proxy,
                 gchar *sender_name,
                 gchar *signal_name,
                 GVariant *parameters,
                 gpointer user_data)
{
	if (g_str_equal (signal_name, "dpms_on_x_off")) {
		gint32 value = 0;
		g_variant_get (parameters, "(i)", &value);
		dpms_off_time_update (value);
	} else if (g_str_equal (signal_name, "sleep_time")) {
		gint32 value = 0;
		g_variant_get (parameters, "(i)", &value);
		sleep_inactive_time_update (value);
	} else if (g_str_equal (signal_name, "agent_msg")) {
		GVariant *v = NULL;
		gchar *data = NULL;
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			data = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
		}
		if (data) {
			show_notification (NULL, data, "dialog-information");
			g_free (data);
		}
	} else if (g_str_equal (signal_name, "update_operation")) {
		gint32 value = -1;
		g_variant_get (parameters, "(i)", &value);
		do_update_operation (value);
	} else if (g_str_equal (signal_name, "app_black_list")) {
		GVariant *v = NULL;
		gchar *blacklist = NULL;
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			blacklist = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
		}
		if (blacklist) {
			save_settings (blacklist, "black_list");
			g_free (blacklist);
		}
	} else if (g_str_equal (signal_name, "controlcenter_items")) {
		GVariant *v = NULL;
		gchar *items = NULL;
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			items = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
		}
		if (items) {
			save_settings (items, "controlcenter_items");
			g_free (items);
		}
	} else if (g_str_equal (signal_name, "add_theme")) {
		GVariant *v = NULL;
		gchar *data = NULL;
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			data = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
			if (data) {
				add_theme (data);
				g_free (data);
			}
		}
	} else if (g_str_equal (signal_name, "delete_theme")) {
		GVariant *v = NULL;
		gchar *data = NULL;
		g_variant_get (parameters, "(v)", &v);
		if (v) {
			data = g_variant_dup_string (v, NULL);
			g_variant_unref (v);
			if (data) {
				delete_theme (data);
				g_free (data);
			}
		}
	}
}

static void
unbind_grac_signal (void)
{
	if (g_grac_proxy && g_grac_signal_id != 0) {
		g_signal_handler_disconnect (g_grac_proxy, g_grac_signal_id);
		g_grac_signal_id = 0;
	}
}

static void
bind_grac_signal (void)
{
	if (!g_grac_proxy)
		g_grac_proxy = proxy_get ("grac");

	if (g_grac_proxy && g_grac_signal_id == 0) {
		g_grac_signal_id = g_signal_connect (g_grac_proxy, "g-signal",
                                             G_CALLBACK (grac_signal_cb), NULL);
	}
}

static void
unbind_gooroom_agent_signal (void)
{
	if (g_agent_proxy && g_agent_signal_id != 0) {
		g_signal_handler_disconnect (g_agent_proxy, g_agent_signal_id);
		g_agent_signal_id = 0;
	}
}

static void
bind_gooroom_agent_signal (void)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy && g_agent_signal_id == 0) {
		g_agent_signal_id = g_signal_connect (g_agent_proxy, "g-signal",
                                              G_CALLBACK (agent_signal_cb), NULL);
	}
}

static gchar *
get_list_from_json (const gchar *data, const gchar *property)
{
	g_return_val_if_fail (data != NULL, NULL);

	gchar *ret = NULL;

	enum json_tokener_error jerr = json_tokener_success;
	json_object *root_obj = json_tokener_parse_verbose (data, &jerr);

	if (jerr == json_tokener_success) {
		json_object *obj1 = NULL, *obj2 = NULL, *obj3 = NULL, *obj4 = NULL;
		obj1 = JSON_OBJECT_GET (root_obj, "module");
		obj2 = JSON_OBJECT_GET (obj1, "task");
		obj3 = JSON_OBJECT_GET (obj2, "out");
		obj4 = JSON_OBJECT_GET (obj3, "status");
		if (obj4) {
			const char *val = json_object_get_string (obj4);
			if (val && g_strcmp0 (val, "200") == 0) {
				json_object *obj = JSON_OBJECT_GET (obj3, property);
				ret = g_strdup (json_object_get_string (obj));
			}
		}
		json_object_put (root_obj);
	}

	return ret;
}

static void
set_controlcenter_whitelist (void)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy) {
		const gchar *json;
		GVariant *variant = NULL;
		gchar *arg = NULL, *data = NULL;

		json = "{\"module\":{\"module_name\":\"config\",\"task\":{\"task_name\":\"get_controlcenter_items\",\"in\":{\"login_id\":\"%s\"}}}}";

		arg = g_strdup_printf (json, g_get_user_name ());

		variant = g_dbus_proxy_call_sync (g_agent_proxy,
                                          "do_task",
                                          g_variant_new ("(s)", arg),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, NULL);

		if (variant) {
			GVariant *v = NULL;
			g_variant_get (variant, "(v)", &v);
			if (v) {
				data = g_variant_dup_string (v, NULL);
				g_variant_unref (v);
			}
			g_variant_unref (variant);
		}

		if (data) {
			gchar *list = get_list_from_json (data, "controlcenter_items");
			if (list) {
				save_settings (list, "controlcenter_items");
				g_free (list);
			}
			g_free (data);
		}

		g_free (arg);
	}
}

static void
set_application_blacklist (void)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy) {
		const gchar *json;
		GVariant *variant = NULL;
		gchar *arg = NULL, *data = NULL;

		json = "{\"module\":{\"module_name\":\"config\",\"task\":{\"task_name\":\"get_app_list\",\"in\":{\"login_id\":\"%s\"}}}}";

		arg = g_strdup_printf (json, g_get_user_name ());

		variant = g_dbus_proxy_call_sync (g_agent_proxy,
                                          "do_task",
                                          g_variant_new ("(s)", arg),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, NULL);

		if (variant) {
			GVariant *v = NULL;
			g_variant_get (variant, "(v)", &v);
			if (v) {
				data = g_variant_dup_string (v, NULL);
				g_variant_unref (v);
			}
			g_variant_unref (variant);
		}

		if (data) {
			gchar *list = get_list_from_json (data, "black_list");
			if (list) {
				save_settings (list, "black_list");
				g_free (list);
			}
			g_free (data);
		}

		g_free (arg);
	}
}

static void
restart_dockbarx_done_cb (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
	if (g_blacklist_settings) {
		g_signal_handlers_unblock_by_func (g_blacklist_settings,
                                           gooroom_blacklist_settings_changed, NULL);
	}
}

static void
gooroom_dockbarx_applet_vanished_cb (GDBusConnection *connection,
                                     const gchar     *name,
                                     gpointer         data)
{
}

static void
gooroom_dockbarx_applet_appeared_cb (GDBusConnection *connection,
                                     const gchar     *name,
                                     const gchar     *name_owner,
                                     gpointer         data)
{
	GDBusProxy *proxy = NULL;

	if (g_gda_watch_id) {
		g_bus_unwatch_name (g_gda_watch_id);
		g_gda_watch_id = 0;
	}

	proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           NULL,
                                           "kr.gooroom.dockbarx.applet",
                                           "/kr/gooroom/dockbarx/applet",
                                           "kr.gooroom.dockbarx.applet",
                                           NULL,
                                           NULL);

	if (proxy == NULL) {
		g_warning ("Failed to get dockbarx applet proxy");
		return;
	}

	g_dbus_proxy_call (proxy,
                       "Restart", g_variant_new ("()"),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       restart_dockbarx_done_cb,
                       NULL);

	g_clear_object (&proxy);
}

static gboolean
request_to_restart_dockbarx_idle (gpointer data)
{
	if (g_gda_watch_id != 0) {
		g_bus_unwatch_name (g_gda_watch_id);
		g_gda_watch_id = 0;
	}

	g_gda_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                       "kr.gooroom.dockbarx.applet",
                                       G_BUS_NAME_WATCHER_FLAGS_NONE,
                                       gooroom_dockbarx_applet_appeared_cb,
                                       gooroom_dockbarx_applet_vanished_cb,
                                       NULL, NULL);

	return FALSE;
}

static void
update_blacklist (gchar **blacklist)
{
	gchar *pkexec, *cmd;

	pkexec = g_find_program_in_path ("pkexec");

	if (blacklist) {
		gchar *str_blacklist = g_strjoinv (" ", blacklist);
		cmd = g_strdup_printf ("%s %s %s", pkexec, GOOROOM_UPDATE_BLACKLIST_HELPER, str_blacklist);
		g_free (str_blacklist);
	} else {
		cmd = g_strdup_printf ("%s %s", pkexec, GOOROOM_UPDATE_BLACKLIST_HELPER);
	}

	g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);

	g_free (pkexec);
	g_free (cmd);
}

static gboolean
update_blacklist_idle (gpointer user_data)
{
	gchar **blacklist = (gchar **)user_data;

	update_blacklist (blacklist);
	g_strfreev (blacklist);

	request_to_restart_dockbarx_idle (NULL);

	if (g_timeout_id > 0) {
		g_source_remove (g_timeout_id);
		g_timeout_id = 0;
	}

	return FALSE;
}

static void
gooroom_blacklist_settings_changed (GSettings *settings,
                                    const gchar *key,
                                    gpointer data)
{
	if (g_str_equal (key, "blacklist")) {
		if (g_timeout_id == 0) {
			gchar **blacklist = g_settings_get_strv (settings, key);
			g_timeout_id = g_idle_add ((GSourceFunc) update_blacklist_idle, blacklist);
		}
	}
}

static void
send_request_to_gooroom_agent (const gchar *request)
{
	if (!g_agent_proxy)
		g_agent_proxy = proxy_get ("agent");

	if (g_agent_proxy) {
		const gchar *json;
		gchar *arg = NULL;
		GVariant *variant = NULL;

		json = "{\"module\":{\"module_name\":\"config\",\"task\":{\"task_name\":\"%s\",\"in\":{\"login_id\":\"%s\"}}}}";

		arg = g_strdup_printf (json, request, g_get_user_name ());

		variant = g_dbus_proxy_call_sync (g_agent_proxy,
                                          "do_task",
                                          g_variant_new ("(s)", arg),
                                          G_DBUS_CALL_FLAGS_NONE, -1,
                                          NULL, NULL);
		if (variant)
			g_variant_unref (variant);

		g_free (arg);
	}
}

static gboolean
logout_session_cb (gpointer data)
{
	g_spawn_command_line_async ("/usr/bin/gooroom-logout-command --logout", NULL);

	return FALSE;
}

static void
terminate_session (void)
{
	GtkWidget *message = gtk_message_dialog_new (NULL,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_OK,
                                                 NULL);

	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (message),
			_("Could not found user's settings file.\nAfter 10 seconds, the user will be logged out."));

	gtk_window_set_title (GTK_WINDOW (message), _("Terminating Session"));

	g_signal_connect (message, "response", G_CALLBACK (gtk_widget_destroy), NULL);

	g_timeout_add (1000 * 10, (GSourceFunc) logout_session_cb, NULL);

	gtk_widget_show (message);
}

static void
grac_name_vanished_cb (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         data)
{
	g_warning ("GRAC Service is not running");

	unbind_grac_signal ();
}

static void
grac_name_appeared_cb (GDBusConnection *connection,
                       const gchar     *name,
                       const gchar     *name_owner,
                       gpointer         data)
{
	bind_grac_signal ();
}

static void
agent_job_thread_done_cb (GObject      *source_object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
	bind_gooroom_agent_signal ();

	if (!g_agent_name_appeared || registered_gpms ()) {
		if (is_systemd_service_active ("grac-device-daemon.service"))
			reload_grac_service ();

		g_idle_add ((GSourceFunc) request_to_restart_dockbarx_idle, NULL);
	}

	g_agent_name_appeared = TRUE;
}

static void
agent_job_thread (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	if (registered_gpms ()) {
		const gchar *req_arg;

		if (g_blacklist_settings) {
			g_signal_handlers_block_by_func (g_blacklist_settings,
                                             gooroom_blacklist_settings_changed, NULL);
		}
		req_arg = is_gpms_user (g_get_user_name ()) ? "set_authority_config" : "set_authority_config_local";

		/* request to save GRAC's rule for Gooroom */
		send_request_to_gooroom_agent (req_arg);
		/* request to check blocking packages change */
		send_request_to_gooroom_agent ("get_update_operation_with_loginid");

		set_dpms_off_time ();
		set_sleep_inactive_time ();
		set_controlcenter_whitelist ();
		set_application_blacklist ();
	}

	/* The task has finished */
	g_task_return_boolean (task, TRUE);
}

static void
gooroom_agent_name_vanished_cb (GDBusConnection *connection,
                                const gchar     *name,
                                gpointer         data)
{
	g_warning ("Gooroom Agent Service is not running");

	unbind_gooroom_agent_signal ();

	if (!g_agent_name_appeared) {
		if (is_systemd_service_active ("grac-device-daemon.service"))
			reload_grac_service ();

		g_idle_add ((GSourceFunc) request_to_restart_dockbarx_idle, NULL);
	}
}

static void
gooroom_agent_name_appeared_cb (GDBusConnection *connection,
                                const gchar     *name,
                                const gchar     *name_owner,
                                gpointer         data)
{
	GTask *task;

	task = g_task_new (NULL, NULL, agent_job_thread_done_cb, NULL);
	g_task_run_in_thread (task, agent_job_thread);
	g_object_unref (task);
}

static void
start_init_thread_done_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
	g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                      "kr.gooroom.agent",
                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                      gooroom_agent_name_appeared_cb,
                      gooroom_agent_name_vanished_cb,
                      NULL, NULL);

	g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                      "kr.gooroom.GRACDEVD",
                      G_BUS_NAME_WATCHER_FLAGS_NONE,
                      grac_name_appeared_cb,
                      grac_name_vanished_cb,
                      NULL, NULL);
}

static void
start_init_thread (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	/* initialize blacklist */
	update_blacklist (NULL);

	/* remove permission from binary */
	gchar **blacklist = g_settings_get_strv (g_blacklist_settings, "blacklist");
	if (blacklist)
		update_blacklist (blacklist);
	g_strfreev (blacklist);

	/* The task has finished */
	g_task_return_boolean (task, TRUE);
}

static void
name_acquired_handler (GDBusConnection *connection,
                       const gchar     *name,
                       gpointer         user_data)
{
	GTask *task;
	gchar *grm_user = NULL;
	GSettingsSchema *schema = NULL;

	grm_user = g_strdup_printf ("%s/.gooroom/%s", g_get_home_dir (), GRM_USER);

	if (is_gpms_user (g_get_user_name ())) {
		if (!g_file_test (grm_user, G_FILE_TEST_EXISTS)) {
			terminate_session ();
			goto done;
		}

		/* configure icon-theme and background */
		handle_desktop_configuration ();
	}

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                              "apps.gooroom-applauncher-applet", TRUE);
	if (schema) {
		g_blacklist_settings = g_settings_new_full (schema, NULL, NULL);
		g_signal_connect (G_OBJECT (g_blacklist_settings), "changed",
                          G_CALLBACK (gooroom_blacklist_settings_changed), NULL);
		g_settings_schema_unref (schema);
	}

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                              "org.gnome.ControlCenter", TRUE);
	if (schema) {
		g_whitelist_settings = g_settings_new_full (schema, NULL, NULL);
		g_settings_schema_unref (schema);
	}

	task = g_task_new (NULL, NULL, start_init_thread_done_cb, NULL);
	g_task_run_in_thread (task, start_init_thread);
	g_object_unref (task);


done:
	g_free (grm_user);
}

static void
name_lost_handler (GDBusConnection *connection,
                   const gchar *name,
                   gpointer user_data)
{
	/* Name was already taken, or the bus went away */
	g_warning ("Name taken or bus went away - shutting down");

	gtk_main_quit ();
}

int
main (int argc, char **argv)
{
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	g_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                 "kr.gooroom.SessionManager",
                                 G_BUS_NAME_OWNER_FLAGS_NONE,
                                 NULL,
                                 (GBusNameAcquiredCallback) name_acquired_handler,
                                 (GBusNameLostCallback) name_lost_handler,
                                 NULL,
                                 NULL);

	gtk_main ();

	if (g_timeout_id > 0) {
		g_source_remove (g_timeout_id);
		g_timeout_id = 0;
	}

	if (g_gda_watch_id) {
		g_bus_unwatch_name (g_gda_watch_id);
		g_gda_watch_id = 0;
	}

	if (g_owner_id) {
		g_bus_unown_name (g_owner_id);
		g_owner_id = 0;
	}

	if (g_agent_proxy) {
		if (g_agent_signal_id)
			g_signal_handler_disconnect (g_agent_proxy, g_agent_signal_id);
		g_object_unref (g_agent_proxy);
	}

	if (g_grac_proxy) {
		if (g_grac_signal_id)
			g_signal_handler_disconnect (g_grac_proxy, g_grac_signal_id);
		g_object_unref (g_grac_proxy);
	}

	if(g_blacklist_settings)
		g_object_unref (g_blacklist_settings);

	if(g_whitelist_settings)
		g_object_unref (g_whitelist_settings);


	return 0;
}
