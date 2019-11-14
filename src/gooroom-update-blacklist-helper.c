/*
 * Copyright (C) 2015-2019 Gooroom <gooroom@gooroom.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Thomas Wood <thomas.wood@intel.com>
 *
 */

#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include "panel-glib.h"


static gchar *
find_full_desktop_id (const gchar *find_str)
{
	gchar *ret = NULL;
	GList *all_apps = NULL, *l = NULL;

	if (!find_str || g_str_equal (find_str, ""))
		return NULL;

	all_apps = g_app_info_get_all ();

	for (l = all_apps; l; l = l->next) {
		GAppInfo *appinfo = G_APP_INFO (l->data);
		if (!appinfo)
			continue;

		const gchar *id;
		GDesktopAppInfo *dt_info;
		gchar *locale_name, *name, *exec;

		dt_info = G_DESKTOP_APP_INFO (appinfo);

		id = g_app_info_get_id (appinfo);
		if (g_str_equal (id, find_str))
			goto find;

		name = g_desktop_app_info_get_string (dt_info, G_KEY_FILE_DESKTOP_KEY_NAME);
		if (name && ((g_utf8_collate (name, find_str) == 0) || (panel_g_utf8_strstrcase (name, find_str) != NULL))) {
			goto find;
		}

		locale_name = g_desktop_app_info_get_locale_string (dt_info, G_KEY_FILE_DESKTOP_KEY_NAME);
		if (locale_name && ((g_utf8_collate (locale_name, find_str) == 0) || (panel_g_utf8_strstrcase (locale_name, find_str) != NULL))) {
			goto find;
		}

		exec = g_desktop_app_info_get_string (dt_info, G_KEY_FILE_DESKTOP_KEY_EXEC);
		if (exec && ((g_utf8_collate (exec, find_str) == 0) || (panel_g_utf8_strstrcase (exec, find_str) != NULL))) {
			goto find;
		}
		continue;

find:
		ret = g_strdup (g_desktop_app_info_get_filename (dt_info));

		g_free (name);
		g_free (locale_name);
		g_free (exec);
		break;
	}
	g_list_free_full (all_apps, g_object_unref);

	return ret;
}

static void
make_exec_executable (const gchar *full_desktop_id, gboolean executable)
{
	g_return_if_fail (full_desktop_id != NULL);

	gchar *exec = NULL;
	gboolean changed = FALSE;
	GDesktopAppInfo *dt_appinfo;

	dt_appinfo = g_desktop_app_info_new_from_filename (full_desktop_id);

	exec = g_desktop_app_info_get_string (dt_appinfo, G_KEY_FILE_DESKTOP_KEY_EXEC);

	if (exec) {
		GStatBuf stat_buf;
		gchar **argv;

		g_shell_parse_argv (exec, NULL, &argv, NULL);

		if (g_strv_length (argv) > 0) {
			gchar *cmd = g_find_program_in_path (argv[0]);

			if (g_stat (cmd, &stat_buf) == 0) {
				mode_t perm = stat_buf.st_mode;
				gboolean cur_exec = perm & S_IXOTH;

				if (executable) {
					if (!cur_exec) {
						g_chmod (cmd, perm | S_IXOTH);
						changed = TRUE;
					}
				} else {
					if (cur_exec) {
						g_chmod (cmd, perm & ~(S_IXOTH));
						changed = TRUE;
					}
				}
			}
			g_free (cmd);
		}
		g_strfreev (argv);
	}

	if (changed) {
		/* For updating libgnome-menu cache */
		GKeyFile *keyfile = g_key_file_new ();
		if (g_key_file_load_from_file (keyfile, full_desktop_id,
					G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
			g_key_file_save_to_file (keyfile, full_desktop_id, NULL);
		}
		g_key_file_free (keyfile);
	}

	g_object_unref (dt_appinfo);
}

static void
init_blacklist (void)
{
	GList *all_apps = NULL, *l = NULL;

	all_apps = g_app_info_get_all ();

	for (l = all_apps; l; l = l->next) {
		GAppInfo *appinfo = G_APP_INFO (l->data);
		if (appinfo) {
			const gchar *full_desktop_id = g_desktop_app_info_get_filename (G_DESKTOP_APP_INFO (appinfo));

			/* don't care gooroomupdate.desktop */
			if (g_str_has_suffix (full_desktop_id, "gooroomupdate.desktop"))
				continue;

			/* restore exec permission */
			make_exec_executable (full_desktop_id, TRUE);
		}
	}

	g_list_free_full (all_apps, g_object_unref);
}

int
main (int argc, char **argv)
{
	init_blacklist ();

	if (argc > 1) {
		guint i = 0;
		for (i = 1; argv[i]; i++) {
			gchar *full_desktop_id = find_full_desktop_id (argv[i]);
			g_debug ("Blacklist Destkop = %s", full_desktop_id);
			if (!g_str_has_suffix (full_desktop_id, "gooroomupdate.desktop")) {
				/* remove exec permission */
				make_exec_executable (full_desktop_id, FALSE);
			}
			g_free (full_desktop_id);
		}
	}

	return 0;
}
