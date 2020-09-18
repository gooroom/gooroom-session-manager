/*
 * Copyright (C) 2004-2008 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008-2011 Red Hat, Inc.
 * Copyright (C) 2019 Gooroom <gooroom@gooroom.kr>.
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
 */

#include "config.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <glib/gi18n.h>

#include "gsm-message-dialog.h"

#ifdef HAVE_SHAPE_EXT
#include <X11/extensions/shape.h>
#endif


struct _GsmMessageDialogPrivate
{
	GdkMonitor   *monitor;

	GdkRectangle  geometry;

	GtkWidget    *lbl_title;
	GtkWidget    *lbl_message;
	GtkWidget    *img_icon;

#ifdef HAVE_SHAPE_EXT
	int        shape_event_base;
#endif
};


enum {
	PROP_0,
	PROP_OBSCURED,
	PROP_MONITOR
};

G_DEFINE_TYPE_WITH_PRIVATE (GsmMessageDialog, gsm_message_dialog, GTK_TYPE_DIALOG)


/* derived from tomboy */
static void
gsm_message_dialog_override_user_time (GsmMessageDialog *window)
{
	guint32 ev_time = gtk_get_current_event_time ();

	if (ev_time == 0) {
		gint ev_mask = gtk_widget_get_events (GTK_WIDGET (window));
		if (!(ev_mask & GDK_PROPERTY_CHANGE_MASK)) {
			gtk_widget_add_events (GTK_WIDGET (window),
					GDK_PROPERTY_CHANGE_MASK);
		}

		/*
		 * NOTE: Last resort for D-BUS or other non-interactive
		 *       openings.  Causes roundtrip to server.  Lame.
		 */
		ev_time = gdk_x11_get_server_time (gtk_widget_get_window (GTK_WIDGET (window)));
	}

	gdk_x11_window_set_user_time (gtk_widget_get_window (GTK_WIDGET (window)), ev_time);
}

static void
update_geometry (GsmMessageDialog *window)
{
	GdkRectangle geometry;

	if (!GDK_IS_MONITOR (window->priv->monitor))
		return;

	gdk_monitor_get_geometry (window->priv->monitor, &geometry);
	g_debug ("got geometry for monitor: x=%d y=%d w=%d h=%d",
             geometry.x, geometry.y, geometry.width, geometry.height);

	window->priv->geometry.x = geometry.x;
	window->priv->geometry.y = geometry.y;
	window->priv->geometry.width = geometry.width;
	window->priv->geometry.height = geometry.height;
}

/* copied from panel-toplevel.c */
static void
gsm_message_dialog_move_resize_window (GsmMessageDialog *window,
                                       gboolean          move,
                                       gboolean          resize)
{
	GdkWindow *gdkwindow;

	gdkwindow = gtk_widget_get_window (GTK_WIDGET (window));

//	g_assert (gtk_widget_get_realized (GTK_WIDGET (window)));

	g_debug ("Move and/or resize window on monitor: x=%d y=%d w=%d h=%d",
			window->priv->geometry.x,
			window->priv->geometry.y,
			window->priv->geometry.width,
			window->priv->geometry.height);

	if (move && resize) {
		gdk_window_move_resize (gdkwindow,
                                window->priv->geometry.x,
                                window->priv->geometry.y,
                                window->priv->geometry.width,
                                window->priv->geometry.height);
	} else if (move) {
		gdk_window_move (gdkwindow,
                         window->priv->geometry.x,
                         window->priv->geometry.y);
	} else if (resize) {
		gdk_window_resize (gdkwindow,
                           window->priv->geometry.width,
                           window->priv->geometry.height);
	}
}

#if 0
static void
screen_size_changed (GdkScreen *screen,
                     GsmMessageDialog  *window)
{
        g_debug ("Got screen size changed signal");
//        gtk_widget_queue_resize (GTK_WIDGET (window));
        gsm_message_dialog_move_resize_window (window, TRUE, TRUE);
}


static void
gsm_message_dialog_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->unrealize (widget);
        }
}
#endif

static void
gsm_message_dialog_real_realize (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->realize)
		GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->realize (widget);

	gsm_message_dialog_override_user_time (GSM_MESSAGE_DIALOG (widget));

//        gsm_message_dialog_move_resize_window (GSM_MESSAGE_DIALOG (widget), TRUE, TRUE);

//        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
//                          "size_changed",
//                          G_CALLBACK (screen_size_changed),
//                          widget);
}

static void
gsm_message_dialog_raise (GsmMessageDialog *window)
{
	GdkWindow *win;

	g_return_if_fail (GSM_IS_MESSAGE_DIALOG (window));

	g_debug ("Raising screensaver window");

	win = gtk_widget_get_window (GTK_WIDGET (window));

	gdk_window_raise (win);
}

static gboolean
x11_window_is_ours (Window window)
{
	GdkWindow *gwindow;
	gboolean   ret;

	ret = FALSE;

	gwindow = gdk_x11_window_lookup_for_display (gdk_display_get_default (), window);
	if (gwindow && (window != GDK_ROOT_WINDOW ())) {
		ret = TRUE;
	}

	return ret;
}

#ifdef HAVE_SHAPE_EXT
static void
unshape_window (GsmMessageDialog *window)
{
	gdk_window_shape_combine_region (gtk_widget_get_window (GTK_WIDGET (window)), NULL, 0, 0);
}
#endif

static void
gsm_message_dialog_xevent (GsmMessageDialog  *window,
                  GdkXEvent *xevent)
{
	XEvent *ev;

	ev = xevent;

	/* MapNotify is used to tell us when new windows are mapped.
	   ConfigureNofify is used to tell us when windows are raised. */
	switch (ev->xany.type) {
		case MapNotify:
			{
				XMapEvent *xme = &ev->xmap;

				if (! x11_window_is_ours (xme->window)) {
					gsm_message_dialog_raise (window);
				} else {
					g_debug ("not raising our windows");
				}

				break;
			}
		case ConfigureNotify:
			{
				XConfigureEvent *xce = &ev->xconfigure;

				if (! x11_window_is_ours (xce->window)) {
					gsm_message_dialog_raise (window);
				} else {
					g_debug ("not raising our windows");
				}

				break;
			}
		default:
			/* extension events */
#ifdef HAVE_SHAPE_EXT
			if (ev->xany.type == (window->priv->shape_event_base + ShapeNotify)) {
				/*XShapeEvent *xse = (XShapeEvent *) ev;*/
				unshape_window (window);
				g_debug ("Window was reshaped!");
			}
#endif

			break;
	}
}

static GdkFilterReturn
xevent_filter (GdkXEvent        *xevent,
               GdkEvent         *event,
               GsmMessageDialog *window)
{
        gsm_message_dialog_xevent (window, xevent);

        return GDK_FILTER_CONTINUE;
}

//static void
//select_popup_events (void)
//{
//        XWindowAttributes attr;
//        unsigned long     events;
//
//        gdk_error_trap_push ();
//
//        memset (&attr, 0, sizeof (attr));
//        XGetWindowAttributes (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_ROOT_WINDOW (), &attr);
//
//        events = SubstructureNotifyMask | attr.your_event_mask;
//        XSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_ROOT_WINDOW (), events);
//
//        gdk_error_trap_pop_ignored ();
//}
//
//static void
//window_select_shape_events (GsmMessageDialog *window)
//{
//#ifdef HAVE_SHAPE_EXT
//        unsigned long events;
//        int           shape_error_base;
//
//        gdk_error_trap_push ();
//
//        if (XShapeQueryExtension (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), &window->priv->shape_event_base, &shape_error_base)) {
//                events = ShapeNotifyMask;
//                XShapeSelectInput (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), GDK_WINDOW_XID (gtk_widget_get_window (GTK_WIDGET (window))), events);
//        }
//
//        gdk_error_trap_pop_ignored ();
//#endif
//}

static gboolean
gsm_message_dialog_real_draw (GtkWidget *widget,
                              cairo_t   *cr)
{
	GsmMessageDialog *window = GSM_MESSAGE_DIALOG (widget);

	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
	cairo_paint (cr);

	if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->draw)
		return GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->draw (widget, cr);

	return FALSE;
}

static void
gsm_message_dialog_real_show (GtkWidget *widget)
{
	GsmMessageDialog *window = GSM_MESSAGE_DIALOG (widget);

	if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->show)
		GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->show (widget);

//	select_popup_events ();
//	window_select_shape_events (window);
	gdk_window_add_filter (NULL, (GdkFilterFunc)xevent_filter, window);
}

static void
gsm_message_dialog_real_hide (GtkWidget *widget)
{
	GsmMessageDialog *window = GSM_MESSAGE_DIALOG (widget);

	gdk_window_remove_filter (NULL, (GdkFilterFunc)xevent_filter, window);

	if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->hide)
		GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->hide (widget);
}

void
gsm_message_dialog_set_monitor (GsmMessageDialog *window,
                                GdkMonitor       *monitor)
{
	g_return_if_fail (GSM_IS_MESSAGE_DIALOG (window));

	if (window->priv->monitor && window->priv->monitor == monitor) {
		return;
	}

	window->priv->monitor = monitor;

	gtk_widget_queue_resize (GTK_WIDGET (window));

	g_object_notify (G_OBJECT (window), "monitor");
}

GdkMonitor *
gsm_message_dialog_get_monitor (GsmMessageDialog *window)
{
	g_return_val_if_fail (GSM_IS_MESSAGE_DIALOG (window), NULL);

	return window->priv->monitor;
}

static void
gsm_message_dialog_real_close (GtkDialog *dialog)
{
//	GTK_DIALOG_CLASS (greeter_ars_dialog_parent_class)->close (dialog);
}

static void
gsm_message_dialog_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	GsmMessageDialog *self;

	self = GSM_MESSAGE_DIALOG (object);

	switch (prop_id) {
		case PROP_MONITOR:
			gsm_message_dialog_set_monitor (self, g_value_get_pointer (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gsm_message_dialog_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	GsmMessageDialog *self;

	self = GSM_MESSAGE_DIALOG (object);

	switch (prop_id) {
		case PROP_MONITOR:
			g_value_set_pointer (value, (gpointer) self->priv->monitor);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

#if 0
static void
gsm_message_dialog_real_size_request (GtkWidget      *widget,
                                      GtkRequisition *requisition)
{
	GsmMessageDialog      *window;
	GdkRectangle   old_geometry;
	int            position_changed = FALSE;
	int            size_changed = FALSE;

	window = GSM_MESSAGE_DIALOG (widget);

	old_geometry = window->priv->geometry;

	update_geometry (window);

	requisition->width = window->priv->geometry.width;
	requisition->height = window->priv->geometry.height;

	if (!gtk_widget_get_realized (widget))
		return;

	if (old_geometry.width  != window->priv->geometry.width ||
        old_geometry.height != window->priv->geometry.height) {
		size_changed = TRUE;
	}

	if (old_geometry.x != window->priv->geometry.x ||
        old_geometry.y != window->priv->geometry.y) {
		position_changed = TRUE;
	}

	gsm_message_dialog_move_resize_window (window, position_changed, size_changed);
}
#endif

//static gboolean
//gsm_message_dialog_real_visibility_notify_event (GtkWidget          *widget,
//                                        GdkEventVisibility *event)
//{
//	switch (event->state) {
//		case GDK_VISIBILITY_FULLY_OBSCURED:
//			window_set_obscured (GSM_MESSAGE_DIALOG (widget), TRUE);
//			break;
//		case GDK_VISIBILITY_PARTIAL:
//			break;
//		case GDK_VISIBILITY_UNOBSCURED:
//			window_set_obscured (GSM_MESSAGE_DIALOG (widget), FALSE);
//			break;
//		default:
//			break;
//	}
//
//	return FALSE;
//}

static void
gsm_message_dialog_real_size_allocate (GtkWidget     *widget,
                                       GtkAllocation *allocation)
{
	GsmMessageDialog *window;
	GtkRequisition requisition;

	window = GSM_MESSAGE_DIALOG (widget);

	if (GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->size_allocate)
		GTK_WIDGET_CLASS (gsm_message_dialog_parent_class)->size_allocate (widget, allocation);

	if (!gtk_widget_get_realized (widget))
		return;

	update_geometry (window);

	gsm_message_dialog_move_resize_window (GSM_MESSAGE_DIALOG (widget), TRUE, TRUE);
}

#if 0
static void
gsm_message_dialog_real_get_preferred_width (GtkWidget *widget,
                                             gint      *minimal_width,
                                             gint      *natural_width)
{
	GtkRequisition requisition;

	gsm_message_dialog_real_size_request (widget, &requisition);

	*minimal_width = *natural_width = requisition.width;
}

static void
gsm_message_dialog_real_get_preferred_height (GtkWidget *widget,
                                              gint      *minimal_height,
                                              gint      *natural_height)
{
	GtkRequisition requisition;

	gsm_message_dialog_real_size_request (widget, &requisition);

	*minimal_height = *natural_height = requisition.height;
}
#endif

#if 0
static void
gsm_message_dialog_finalize (GObject *object)
{
        GsmMessageDialog *window = GSM_MESSAGE_DIALOG (object);

        G_OBJECT_CLASS (gsm_message_dialog_parent_class)->finalize (object);
}
#endif

static void
gsm_message_dialog_constructed (GObject *object)
{
	GsmMessageDialog *window = GSM_MESSAGE_DIALOG (object);
	GsmMessageDialogPrivate *priv = window->priv;

    G_OBJECT_CLASS (gsm_message_dialog_parent_class)->constructed (object);

	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_window_fullscreen (GTK_WINDOW (window));

//	gtk_widget_set_events (GTK_WIDGET (window),
//                           gtk_widget_get_events (GTK_WIDGET (window))
//                           | GDK_POINTER_MOTION_MASK
//                           | GDK_BUTTON_PRESS_MASK
//                           | GDK_BUTTON_RELEASE_MASK
//                           | GDK_KEY_PRESS_MASK
//                           | GDK_KEY_RELEASE_MASK
//                           | GDK_EXPOSURE_MASK
//                           | GDK_VISIBILITY_NOTIFY_MASK
//                           | GDK_ENTER_NOTIFY_MASK
//                           | GDK_LEAVE_NOTIFY_MASK);

	GdkScreen *screen = gtk_window_get_screen (GTK_WINDOW (window));
	if(gdk_screen_is_composited (screen)) {
		GdkVisual *visual = gdk_screen_get_rgba_visual (screen);
		if (visual == NULL)
			visual = gdk_screen_get_system_visual (screen);

		gtk_widget_set_visual (GTK_WIDGET (window), visual);
	}
}

static void
gsm_message_dialog_init (GsmMessageDialog *window)
{
	GsmMessageDialogPrivate *priv;
	priv = window->priv = gsm_message_dialog_get_instance_private (window);

	priv->geometry.x      = -1;
	priv->geometry.y      = -1;
	priv->geometry.width  = -1;
	priv->geometry.height = -1;

	gtk_widget_init_template (GTK_WIDGET (window));
}

static void
gsm_message_dialog_class_init (GsmMessageDialogClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (klass);

	dialog_class->close = gsm_message_dialog_real_close;

	object_class->constructed = gsm_message_dialog_constructed;
	object_class->get_property = gsm_message_dialog_get_property;
	object_class->set_property = gsm_message_dialog_set_property;
//	object_class->finalize     = gsm_message_dialog_finalize;

	widget_class->show                    = gsm_message_dialog_real_show;
	widget_class->draw                    = gsm_message_dialog_real_draw;
	widget_class->size_allocate           = gsm_message_dialog_real_size_allocate;
	widget_class->hide                    = gsm_message_dialog_real_hide;
	widget_class->realize                 = gsm_message_dialog_real_realize;
//	widget_class->unrealize               = gsm_message_dialog_real_unrealize;
//	widget_class->get_preferred_width     = gsm_message_dialog_real_get_preferred_width;
//	widget_class->get_preferred_height    = gsm_message_dialog_real_get_preferred_height;
//	widget_class->visibility_notify_event = gsm_message_dialog_real_visibility_notify_event;

	gtk_widget_class_set_template_from_resource (widget_class,
                                                 "/kr/gooroom/session-manager/gsm-message-dialog.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsmMessageDialog, lbl_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsmMessageDialog, img_icon);
	gtk_widget_class_bind_template_child_private (widget_class, GsmMessageDialog, lbl_message);


	g_object_class_install_property (object_class,
                                     PROP_MONITOR,
                                     g_param_spec_pointer ("monitor",
                                                           "Gdk monitor",
                                                           "The monitor (in terms of Gdk) which the window is on",
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

GsmMessageDialog *
gsm_message_dialog_new (GdkDisplay *display,
                        GdkMonitor *monitor)
{
	GObject   *result;
	GdkScreen *screen;

	screen = gdk_display_get_default_screen (display);

	result = g_object_new (GSM_TYPE_MESSAGE_DIALOG,
                           "screen", screen,
                           "monitor", monitor,
                           "app-paintable", TRUE,
                           NULL);

	return GSM_MESSAGE_DIALOG (result);
}

void
gsm_message_dialog_set_title (GsmMessageDialog *window,
                              const char       *title)
{
	if (title)
		gtk_label_set_text (GTK_LABEL (window->priv->lbl_title), title);
}

void
gsm_message_dialog_set_message (GsmMessageDialog *window,
                                const char       *message)
{
	if (message)
		gtk_label_set_text (GTK_LABEL (window->priv->lbl_message), message);
}

void
gsm_message_dialog_set_icon_name (GsmMessageDialog *window,
                                  const char       *icon_name)
{
	if (icon_name)
		gtk_image_set_from_icon_name (GTK_IMAGE (window->priv->img_icon),
                                      icon_name,
                                      GTK_ICON_SIZE_DIALOG);
}
