/*
 * panel-glib.c: various small extensions to glib
 *
 * Copyright (C) 2008 Novell, Inc.
 *
 * Originally based on code from panel-util.c (there was no relevant copyright
 * header at the time), but the code was:
 * Copyright (C) Novell, Inc. (for the panel_g_utf8_strstrcase() code)
 * Copyright (C) Dennis Cranston (for the panel_g_lookup_in_data_dirs() code)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <string.h>

#include <glib.h>

#include "panel-glib.h"

/* Copied from evolution-data-server/libedataserver/e-util.c:
 * e_util_unicode_get_utf8() */
static char *
_unicode_get_utf8 (const char *text, gunichar *out)
{
	*out = g_utf8_get_char (text);
	return (*out == (gunichar)-1) ? NULL : g_utf8_next_char (text);
}

/* Copied from evolution-data-server/libedataserver/e-util.c:
 * e_util_utf8_strstrcase() */
const char *
panel_g_utf8_strstrcase (const char *haystack, const char *needle)
{
	gunichar *nuni;
	gunichar unival;
	gint nlen;
	const char *o, *p;

	if (haystack == NULL) return NULL;
	if (needle == NULL) return NULL;
	if (strlen (needle) == 0) return haystack;
	if (strlen (haystack) == 0) return NULL;

	nuni = g_alloca (sizeof (gunichar) * strlen (needle));

	nlen = 0;
	for (p = _unicode_get_utf8 (needle, &unival);
	     p && unival;
	     p = _unicode_get_utf8 (p, &unival)) {
		nuni[nlen++] = g_unichar_tolower (unival);
	}
	/* NULL means there was illegal utf-8 sequence */
	if (!p) return NULL;

	o = haystack;
	for (p = _unicode_get_utf8 (o, &unival);
	     p && unival;
	     p = _unicode_get_utf8 (p, &unival)) {
		gunichar sc;
		sc = g_unichar_tolower (unival);
		/* We have valid stripped char */
		if (sc == nuni[0]) {
			const char *q = p;
			gint npos = 1;
			while (npos < nlen) {
				q = _unicode_get_utf8 (q, &unival);
				if (!q || !unival) return NULL;
				sc = g_unichar_tolower (unival);
				if (sc != nuni[npos]) break;
				npos++;
			}
			if (npos == nlen) {
				return o;
			}
		}
		o = p;
	}

	return NULL;
}
