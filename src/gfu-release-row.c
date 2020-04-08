/*
 * Copyright (C) 2012-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gfu-release-row.h"

typedef struct {
	FwupdRelease	*release;
	GtkWidget	*image;
	GtkWidget	*name;
	GtkWidget	*version;
	guint		 pending_refresh_id;
} GfuReleaseRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GfuReleaseRow, gfu_release_row, GTK_TYPE_LIST_BOX_ROW)

static void
gfu_release_row_refresh (GfuReleaseRow *self)
{
	const gchar *tmp;

	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);
	if (priv->release == NULL)
		return;

	tmp = fwupd_release_get_name (priv->release);
	gtk_label_set_label (GTK_LABEL (priv->name), tmp);
	tmp = fwupd_release_get_version (priv->release);
	gtk_label_set_label (GTK_LABEL (priv->version), tmp);
	gtk_widget_set_visible (priv->version, TRUE);

	/* TODO: set icon, e.g. security-high */
}

FwupdRelease *
gfu_release_row_get_release (GfuReleaseRow *self)
{
	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);
	g_return_val_if_fail (GFU_IS_RELEASE_ROW (self), NULL);
	return priv->release;
}

static gboolean
gfu_release_row_refresh_idle_cb (gpointer user_data)
{
	GfuReleaseRow *self = GFU_RELEASE_ROW (user_data);
	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);
	priv->pending_refresh_id = 0;
	gfu_release_row_refresh (self);
	return FALSE;
}

static void
gfu_release_row_notify_props_changed_cb (FwupdRelease *release,
				        GParamSpec *pspec,
				        GfuReleaseRow *self)
{
	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);
	if (priv->pending_refresh_id > 0)
		return;
	priv->pending_refresh_id = g_idle_add (gfu_release_row_refresh_idle_cb, self);
}

static void
gfu_release_row_set_release (GfuReleaseRow *self, FwupdRelease *release)
{
	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);

	priv->release = g_object_ref (release);

	g_signal_connect_object (priv->release, "notify::state",
				 G_CALLBACK (gfu_release_row_notify_props_changed_cb),
				 self, 0);
	gfu_release_row_refresh (self);
}

static void
gfu_release_row_destroy (GtkWidget *object)
{
	GfuReleaseRow *self = GFU_RELEASE_ROW (object);
	GfuReleaseRowPrivate *priv = gfu_release_row_get_instance_private (self);

	if (priv->release != NULL)
		g_signal_handlers_disconnect_by_func (priv->release, gfu_release_row_notify_props_changed_cb, self);

	g_clear_object (&priv->release);
	if (priv->pending_refresh_id != 0) {
		g_source_remove (priv->pending_refresh_id);
		priv->pending_refresh_id = 0;
	}

	GTK_WIDGET_CLASS (gfu_release_row_parent_class)->destroy (object);
}

static void
gfu_release_row_class_init (GfuReleaseRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->destroy = gfu_release_row_destroy;
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Firmware/gfu-release-row.ui");
	gtk_widget_class_bind_template_child_private (widget_class, GfuReleaseRow, image);
	gtk_widget_class_bind_template_child_private (widget_class, GfuReleaseRow, name);
	gtk_widget_class_bind_template_child_private (widget_class, GfuReleaseRow, version);
}

static void
gfu_release_row_init (GfuReleaseRow *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
gfu_release_row_new (FwupdRelease *release)
{
	GtkWidget *self;
	g_return_val_if_fail (FWUPD_IS_RELEASE (release), NULL);
	self = g_object_new (GFU_TYPE_RELEASE_ROW, NULL);
	gfu_release_row_set_release (GFU_RELEASE_ROW (self), release);
	return self;
}
