#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <json-c/json.h>

#define BACKGROUND_PATH "/usr/share/backgrounds/gooroom"
#define ICONS_PATH "/usr/share/icons"
#define THEME_CONTROL_FILE "/usr/share/gnome-control-center/themes/gooroom-themes.ini"

static const char *g_index_template =
"[Icon Theme]\nName=%s\nComment=%s\nInherits=Gooroom-Numix-Circle\n\n\
# Directory list\nDirectories=apps/scalable\n\n\
[apps/scalable]\nContext=Applications\nSize=48\nMinSize=16\nMaxSize=512\nType=Scalable\n";

static const char *g_theme_template =
"[User Theme %s]\nName=User Theme %s\nName[ko]=%s\nName[ko_KR]=%s\nName[ko_KR.UTF-8]=%s\nBackground=%s\nIcon=%s\n";

static json_object *
JSON_OBJECT_GET (json_object *root_obj, const char *key)
{
	if (!root_obj) return NULL;

	json_object *ret_obj = NULL;

	json_object_object_get_ex (root_obj, key, &ret_obj);

	return ret_obj;
}

static void
download (char *url, char *dest)
{
	gboolean valid = g_uri_is_valid (url, G_URI_FLAGS_NONE, NULL);

	if (!valid) return;

	gchar *wget = g_find_program_in_path ("wget");
	if (wget) {
		gchar *cmd = g_strdup_printf ("%s --no-check-certificate %s -q -O %s", wget, url, dest);
		g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);
		g_free (cmd);
	}
	g_free (wget);
	g_free (url);
}

static void
filecopy (const char *src, const char*dest)
{
		gchar *cmd = g_strdup_printf ("cp %s %s", src, dest);
		g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);
		g_free (cmd);
}

static void
create_theme_file_from_data (gchar *file_path, gchar *data, gboolean is_delete, gboolean is_append)
{
	g_autoptr(GFile) tmp_file = NULL;
	g_autoptr (GFileIOStream) iostream = NULL;
	GOutputStream *outstream = NULL;

	tmp_file = g_file_new_for_path (file_path);

	if (is_delete) {
		if (g_file_query_exists (tmp_file, NULL) && !g_file_delete (tmp_file, NULL, NULL))
			return;
	}

	if (is_append) {
		outstream = G_OUTPUT_STREAM (g_file_append_to (tmp_file, G_FILE_COPY_NONE, NULL, NULL));
	}
	else {
		iostream = g_file_create_readwrite (tmp_file, G_FILE_COPY_NONE, NULL, NULL);
		if (iostream == NULL)
			return;

		outstream = g_io_stream_get_output_stream (G_IO_STREAM (iostream));
	}

	g_output_stream_write_all (outstream, data, strlen(data), NULL, NULL, NULL);
	g_output_stream_close (outstream, NULL, NULL);

	g_free (file_path);
	g_free (data);
}

static void
add_theme (char *type, char *data)
{
	gchar *background_path = NULL;

	const char *theme_id = NULL, *theme_name = NULL, *theme_comment = NULL;
	const char *theme_background= NULL;

	enum json_tokener_error jerr = json_tokener_success;
	json_object *root_obj = json_tokener_parse_verbose (data, &jerr);
	json_object *obj1 = NULL, *obj2 = NULL, *obj3 = NULL, *obj4 = NULL, *obj5 = NULL;

	if (jerr != json_tokener_success) return;

	obj1 = JSON_OBJECT_GET (root_obj, "themeId");
	obj2 = JSON_OBJECT_GET (root_obj, "themeNm");
	obj3 = JSON_OBJECT_GET (root_obj, "themeCmt");

	if (!(obj1 && obj2 && obj3)) return;

	obj4 = JSON_OBJECT_GET (root_obj, "wallpaperUrl");
	obj5 = JSON_OBJECT_GET (root_obj, "themeIcons");

	theme_id = json_object_get_string (obj1);
	theme_name = json_object_get_string (obj2);
	theme_comment = json_object_get_string (obj3);

	if (obj4) {
		background_path = g_strdup_printf ("%s/gooroom_user_theme_bg_%s.png", BACKGROUND_PATH, theme_id);
		theme_background = json_object_get_string (obj4);
		download (g_strdup(theme_background), background_path);
	}

	if (obj5) {
		gint i = 0, len = 0;
		const gchar* icon_path = g_strdup_printf ("%s/%s/apps/scalable", ICONS_PATH, theme_id);
		if (g_mkdir_with_parents (icon_path, 0755) != 0) return;

		len = json_object_array_length (obj5);
		//icon download
		for (i = 0; i < len; i++) {
			json_object *obj = json_object_array_get_idx (obj5, i);

			if (!obj) continue;

			const char *name = NULL, *url = NULL, *info = NULL, *fullname = NULL;
			json_object *name_obj = NULL, *url_obj = NULL, *info_obj = NULL;

			json_object_object_get_ex (obj, "fileName", &name_obj);
			json_object_object_get_ex (obj, "fileEtcInfo", &info_obj);
			json_object_object_get_ex (obj, "imgUrl", &url_obj);

			if (!(name_obj && info_obj && url_obj)) continue;

			name = json_object_get_string (name_obj);
			//if (g_str_has_prefix (name, "4_"))  continue;

			info = json_object_get_string (info_obj);

			gchar **tokens = g_strsplit (name, ".", -1);
			gint length =  g_strv_length (tokens) - 1;

			if (tokens != NULL && tokens[length] != NULL) {
				fullname = g_strdup_printf ("%s.%s", info, tokens[length]);
			}
			g_strfreev (tokens);

			url = json_object_get_string (url_obj);
			gchar *dest = g_strdup_printf ("%s/%s",icon_path, fullname);
			download (g_strdup (url), dest);
			g_free (dest);
		}
	}

	if (g_strcmp0 (type, "0") == 0) {
		//create index file;
		gchar *index_file = g_strdup_printf ("%s/%s/index.theme",ICONS_PATH, theme_id);
		gchar *index_template = g_strdup_printf (g_index_template, theme_name, theme_comment);
		create_theme_file_from_data (index_file, index_template, TRUE, FALSE);
		//control-center theme
		const gchar *backgroundfile = g_strdup_printf ("file://%s", background_path);
		gchar *theme_template = g_strdup_printf (g_theme_template, theme_id, theme_id, theme_name, theme_name, theme_name, backgroundfile, theme_id);
		create_theme_file_from_data (g_strdup(THEME_CONTROL_FILE), theme_template, FALSE, TRUE);
	}
	//control-center thumbnail
	const gchar *thumbnail_path= g_strdup_printf ("%s/%s/thumbnail.png",ICONS_PATH, theme_id);
	filecopy (background_path, thumbnail_path);

	g_free (background_path);
	json_object_put (obj1);
	return;
}

int
main(int argc, char **argv)
{
	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	if (argc > 1)
		add_theme (argv[1], argv[2]);

	return 0;
}
