/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __MESSAGE_DIALOG_H__
#define __MESSAGE_DIALOG_H__

#include <gdk/gdk.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GSM_TYPE_MESSAGE_DIALOG         (gsm_message_dialog_get_type ())
#define GSM_MESSAGE_DIALOG(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSM_TYPE_MESSAGE_DIALOG, GsmMessageDialog))
#define GSM_MESSAGE_DIALOG_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSM_TYPE_MESSAGE_DIALOG, GsmMessageDialogClass))
#define GSM_IS_MESSAGE_DIALOG(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSM_TYPE_MESSAGE_DIALOG))
#define GSM_IS_MESSAGE_DIALOG_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSM_TYPE_MESSAGE_DIALOG))
#define GSM_MESSAGE_DIALOG_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSM_MESSAGE_DIALOG, GsmMessageDialogClass))

typedef struct _GsmMessageDialogPrivate GsmMessageDialogPrivate;
typedef struct _GsmMessageDialogClass   GsmMessageDialogClass;
typedef struct _GsmMessageDialog        GsmMessageDialog;


struct _GsmMessageDialog
{
	GtkDialog  __parent__;

	GsmMessageDialogPrivate *priv;
};

struct _GsmMessageDialogClass
{
	GtkDialogClass   __parent_class__;

//        gboolean        (* activity)            (GsmMessageDialog *window);
//        void            (* deactivated)         (GsmMessageDialog *window);
};

GType       gsm_message_dialog_get_type           (void);

GsmMessageDialog  * gsm_message_dialog_new        (GdkDisplay       *display,
                                                   GdkMonitor       *monitor);

void        gsm_message_dialog_set_screen         (GsmMessageDialog *window,
                                                   GdkScreen        *screen);
GdkScreen * gsm_message_dialog_get_screen         (GsmMessageDialog *window);

void        gsm_message_dialog_set_monitor        (GsmMessageDialog *window,
                                                   GdkMonitor       *monitor);
GdkMonitor *gsm_message_dialog_get_monitor        (GsmMessageDialog *window);

void        gsm_message_dialog_set_title          (GsmMessageDialog *window,
                                                   const char       *title);
void        gsm_message_dialog_set_message        (GsmMessageDialog *window,
                                                   const char       *message);
void        gsm_message_dialog_set_icon_name      (GsmMessageDialog *window,
                                                   const char       *icon_name);
void        gsm_message_dialog_set_buttons        (GsmMessageDialog *window,
                                                   GtkButtonsType    buttons);

G_END_DECLS

#endif /* __MESSAGE_DIALOG_H */
