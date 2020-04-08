/*
 * Copyright (C) 2012-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gfu-device-row.h"

typedef struct {
	FwupdDevice	*device;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*summary;
	guint		 pending_refresh_id;
} GfuDeviceRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GfuDeviceRow, gfu_device_row, GTK_TYPE_LIST_BOX_ROW)

static void
gfu_device_row_refresh (GfuDeviceRow *self)
{
	const gchar *tmp;
	GPtrArray *icons;
	g_autoptr(GIcon) icon = g_themed_icon_new ("computer");

	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);
	if (priv->device == NULL)
		return;

	/* row labels - name, summary */
	tmp = fwupd_device_get_name (priv->device);
	gtk_label_set_label (GTK_LABEL (priv->name), tmp);
	tmp = fwupd_device_get_summary (priv->device);
	gtk_label_set_label (GTK_LABEL (priv->summary), tmp);
	gtk_widget_set_visible (priv->summary, TRUE);

	/* set icon, with fallbacks */
	icons = fwupd_device_get_icons (priv->device);
	for (guint i = 0; i < icons->len; i++) {
		const gchar *icon_name = g_ptr_array_index (icons, i);
		g_themed_icon_prepend_name (G_THEMED_ICON (icon), icon_name);
	}
	gtk_image_set_from_gicon (GTK_IMAGE (priv->image), icon, -1);
}

FwupdDevice *
gfu_device_row_get_device (GfuDeviceRow *self)
{
	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);
	g_return_val_if_fail (GFU_IS_DEVICE_ROW (self), NULL);
	return priv->device;
}

static gboolean
gfu_device_row_refresh_idle_cb (gpointer user_data)
{
	GfuDeviceRow *self = GFU_DEVICE_ROW (user_data);
	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);
	priv->pending_refresh_id = 0;
	gfu_device_row_refresh (self);
	return FALSE;
}

static void
gfu_device_row_notify_props_changed_cb (FwupdDevice *device,
				        GParamSpec *pspec,
				        GfuDeviceRow *self)
{
	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);
	if (priv->pending_refresh_id > 0)
		return;
	priv->pending_refresh_id = g_idle_add (gfu_device_row_refresh_idle_cb, self);
}

static void
gfu_device_row_set_device (GfuDeviceRow *self, FwupdDevice *device)
{
	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);

	priv->device = g_object_ref (device);

	g_signal_connect_object (priv->device, "notify::state",
				 G_CALLBACK (gfu_device_row_notify_props_changed_cb),
				 self, 0);
	gfu_device_row_refresh (self);
}

static void
gfu_device_row_destroy (GtkWidget *object)
{
	GfuDeviceRow *self = GFU_DEVICE_ROW (object);
	GfuDeviceRowPrivate *priv = gfu_device_row_get_instance_private (self);

	if (priv->device != NULL)
		g_signal_handlers_disconnect_by_func (priv->device, gfu_device_row_notify_props_changed_cb, self);

	g_clear_object (&priv->device);
	if (priv->pending_refresh_id != 0) {
		g_source_remove (priv->pending_refresh_id);
		priv->pending_refresh_id = 0;
	}

	GTK_WIDGET_CLASS (gfu_device_row_parent_class)->destroy (object);
}

static void
gfu_device_row_class_init (GfuDeviceRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->destroy = gfu_device_row_destroy;
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Firmware/gfu-device-row.ui");
	gtk_widget_class_bind_template_child_private (widget_class, GfuDeviceRow, image);
	gtk_widget_class_bind_template_child_private (widget_class, GfuDeviceRow, name);
	gtk_widget_class_bind_template_child_private (widget_class, GfuDeviceRow, summary);
}

static void
gfu_device_row_init (GfuDeviceRow *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
gfu_device_row_new (FwupdDevice *device)
{
	GtkWidget *self;
	g_return_val_if_fail (FWUPD_IS_DEVICE (device), NULL);
	self = g_object_new (GFU_TYPE_DEVICE_ROW, NULL);
	gfu_device_row_set_device (GFU_DEVICE_ROW (self), device);
	return self;
}
