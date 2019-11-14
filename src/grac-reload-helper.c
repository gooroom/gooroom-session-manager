/*
 * Copyright (C) 2013 Intel, Inc
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

#include <gio/gio.h>


static gint
reload_service (const char *service)
{
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reload_result = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!connection)
    {
      g_critical ("Error connecting to D-Bus system bus: %s", error->message);
      return 1;
    }

  const gchar *service_list[] = {service, NULL};

  reload_result = g_dbus_connection_call_sync (connection,
                                              "org.freedesktop.systemd1",
                                              "/org/freedesktop/systemd1",
                                              "org.freedesktop.systemd1.Manager",
                                              "ReloadUnit",
                                              g_variant_new ("(ss)", service, "replace"),
                                              (GVariantType *) "(o)",
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,
                                              NULL,
                                              &error);

  if (!reload_result)
    {
      g_critical ("Error starting %s: %s", service, error->message);
      return 1;
    }

  return 0;
}

int
main (int argc, char **argv)
{
	return reload_service ("grac-device-daemon.service");
}
