/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <locale.h>
#include <stdlib.h>
#include <fwupd.h>

#include "gfu-device-row.h"
#include "gfu-release-row.h"
#include "gfu-common.c"

/* gfu types */

typedef enum {
	GFU_MAIN_MODE_UNKNOWN,
	GFU_MAIN_MODE_DEVICE,
	GFU_MAIN_MODE_RELEASE,
	GFU_MAIN_MODE_LAST
} GfuMainMode;

typedef struct {
	GtkApplication		*application;
	GtkBuilder		*builder;
	GCancellable		*cancellable;
	FwupdClient		*client;
	FwupdDevice		*device;
	FwupdRelease		*release;
	GPtrArray		*releases;
	GfuMainMode		 mode;
	GDBusProxy		*proxy;
	SoupSession		*soup_session;
	FwupdInstallFlags	 flags;
	GfuOperation		 current_operation;
	GTimer			*time_elapsed;
	gdouble			 last_estimate;
} GfuMain;

/* used to compare rows in a list */

typedef struct {
	GtkListBox *list;
	FwupdDevice *row;
} GfuDeviceRowHelper;

/* GTK helper functions */

static void
gfu_main_container_remove_all_cb (GtkWidget *widget, gpointer user_data)
{
	GtkContainer *container = GTK_CONTAINER (user_data);
	gtk_container_remove (container, widget);
}

static void
gfu_main_container_remove_all (GtkContainer *container)
{
	gtk_container_foreach (container, gfu_main_container_remove_all_cb, container);
}

static void
gfu_main_error_dialog (GfuMain *self, const gchar *title, const gchar *message)
{
	GtkWindow *window;
	GtkWidget *dialog;

	window = GTK_WINDOW (gtk_builder_get_object (self->builder, "dialog_main"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	if (message != NULL) {
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "%s", message);
	}
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
gfu_main_activate_cb (GApplication *application, GfuMain *self)
{
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (self->builder, "dialog_main"));
	gtk_window_present (window);
}

static void
gfu_main_set_label (GfuMain *self, const gchar *label_id, const gchar *text)
{
	GtkWidget *w;
	g_autofree gchar *label_id_title = g_strdup_printf ("%s_title", label_id);

	/* hide empty box */
	if (text == NULL) {
		w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id_title));
		gtk_widget_set_visible (w, FALSE);
		return;
	}

	/* update and display */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id));
	gtk_label_set_label (GTK_LABEL (w), text);
	gtk_widget_set_visible (w, TRUE);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id_title));
	gtk_widget_set_visible (w, TRUE);
}

static void
gfu_main_set_label_title (GfuMain *self, const gchar *label_id, const gchar *text)
{
	/* update only the title of a label */
	GtkWidget *w;
	g_autofree gchar *label_id_title = g_strdup_printf ("%s_title", label_id);

	/* hide empty box */
	if (text == NULL) {
		w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id));
		gtk_widget_set_visible (w, FALSE);
		w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id_title));
		gtk_widget_set_visible (w, FALSE);
		return;
	}

	/* update and display */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id_title));
	gtk_label_set_label (GTK_LABEL (w), text);
	gtk_widget_set_visible (w, TRUE);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, label_id));
	gtk_widget_set_visible (w, TRUE);
}

static void
gfu_main_set_device_flags (GfuMain *self, guint64 flags)
{
	GtkWidget *icon, *label;
	GtkGrid *w;
	gint count = 0;
	g_autoptr(GString) flag = g_string_new (NULL);

	w = GTK_GRID (gtk_builder_get_object (self->builder, "grid_device_flags"));
	/* clear the grid */
	gtk_grid_remove_column (w, 0);
	gtk_grid_remove_column (w, 0);
	gtk_grid_insert_column (w, 0);
	gtk_grid_insert_column (w, 0);

	/* iterate through flags */
	for (guint j = 0; j < 64; j++) {
		gchar *flag_tmp;
		/* if flag is not set, skip */
		if ((flags & ((guint64) 1 << j)) == 0)
			continue;
		/* else, add a row for this flag */
		g_string_set_size (flag, 0);
		flag_tmp = gfu_common_device_flag_to_string ((guint64) 1 << j);
		if (flag_tmp == NULL)
			continue;
		g_string_assign (flag, flag_tmp);
		gtk_grid_insert_row (w, count);

		icon = gtk_image_new_from_icon_name (gfu_common_device_icon_from_flag ((guint64) 1 << j), GTK_ICON_SIZE_BUTTON);
		gtk_widget_set_visible (icon, TRUE);
		gtk_grid_attach (w, icon, 0, count, 1, 1);

		label = gtk_label_new (flag->str);
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_widget_set_visible (label, TRUE);
		gtk_grid_attach (w, label, 1, count, 1, 1);

		count++;
	}
	if (count == 0) {
		/* no flags were added */
		gtk_widget_set_visible (GTK_WIDGET (w), FALSE);
		gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (self->builder, "label_device_flags_title")), FALSE);
	} else {
		gtk_widget_set_visible (GTK_WIDGET (w), TRUE);
		gtk_widget_set_visible (GTK_WIDGET (gtk_builder_get_object (self->builder, "label_device_flags_title")), TRUE);
	}
}

static void
gfu_main_refresh_ui (GfuMain *self)
{
	GtkWidget *w;
	g_autoptr(GPtrArray) upgrades = NULL;
	gboolean verification_matched = FALSE;

	/* set stack */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "stack_main"));
	if (self->mode == GFU_MAIN_MODE_RELEASE)
		gtk_stack_set_visible_child_name (GTK_STACK (w), "firmware");
	else if (self->mode == GFU_MAIN_MODE_DEVICE)
		gtk_stack_set_visible_child_name (GTK_STACK (w), "main");
	else
		gtk_stack_set_visible_child_name (GTK_STACK (w), "loading");

	/* set device information */
	if (self->device != NULL) {
		GPtrArray *guids;
		g_autoptr(GString) attr = g_string_new (NULL);
		g_autoptr(GError) error = NULL;
		gchar *tmp;
		gchar *tmp2;

		gfu_main_set_label (self, "label_device_version",
				    fwupd_device_get_version (self->device));
		gfu_main_set_label (self, "label_device_version_lowest",
				    fwupd_device_get_version_lowest (self->device));
		gfu_main_set_label (self, "label_device_version_bootloader",
				    fwupd_device_get_version_bootloader (self->device));
		gfu_main_set_label (self, "label_device_update_error",
				    fwupd_device_get_update_error (self->device));
		gfu_main_set_label (self, "label_device_serial",
				    fwupd_device_get_serial (self->device));

		tmp = fwupd_device_get_vendor (self->device);
		tmp2 = fwupd_device_get_vendor_id (self->device);
		if (tmp != NULL && tmp2 != NULL) {
			g_autofree gchar *both = g_strdup_printf ("%s (%s)", tmp, tmp2);
			gfu_main_set_label (self, "label_device_vendor", both);
		} else if (tmp != NULL) {
			gfu_main_set_label (self, "label_device_vendor", tmp);
		} else if (tmp2 != NULL) {
			gfu_main_set_label (self, "label_device_vendor", tmp2);
		} else {
			gfu_main_set_label (self, "label_device_vendor", NULL);
		}

		g_string_append_printf (attr, "%u", fwupd_device_get_flashes_left (self->device));
		gfu_main_set_label (self, "label_device_flashes_left",
				    g_strcmp0 (attr->str, "0")? attr->str : NULL);

		gfu_main_set_label (self, "label_device_install_duration",
				    gfu_common_seconds_to_string (fwupd_device_get_install_duration (self->device)));


		gfu_main_set_device_flags (self, fwupd_device_get_flags (self->device));

		g_string_set_size (attr, 0);
		guids = fwupd_device_get_guids (self->device);
		/* extract GUIDs, append with newline */
		for (guint i = 0; i < guids->len; i++) {
			g_string_append_printf (attr, "%s\n", (gchar *) g_ptr_array_index (guids, i));
		}
		/* remove final newline, set label */
		if (attr->len > 0)
			g_string_truncate (attr, attr->len - 1);
		gfu_main_set_label (self, "label_device_guids", attr->str);
		/* set GUIDs->GUID if only one */
		gfu_main_set_label_title (self, "label_device_guids", ngettext ("GUID", "GUIDs", guids->len));

#if FWUPD_CHECK_VERSION(1,3,3)
		/* device can be verified immediately without a round trip to firmware */
		if (fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_CAN_VERIFY) &&
		    !fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
			if (!fwupd_client_verify (self->client,
						 fwupd_device_get_id (self->device),
						 self->cancellable,
						 &error)) {
				gfu_main_set_label (self, "label_device_checksums", error->message);
			} else {
				gfu_main_set_label (self, "label_device_checksums", _("Cryptographic hashes match"));
				verification_matched = TRUE;
			}

			if (fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE))
				gfu_main_set_label_title (self, "label_device_checksums", _("Firmware checksum"));
			else
				gfu_main_set_label_title (self, "label_device_checksums", _("Device checksum"));
		} else {
			gfu_main_set_label (self, "label_device_checksums", NULL);
		}
#else
		gfu_main_set_label (self, "label_device_checksums", NULL);
#endif
		//fixme: get parents
		//FwupdDevice *parent = fwupd_device_get_parent (self->device);
		//g_print ("parent: %s\n", fwupd_device_get_name (parent));
	}

	/* set release information */
	if (self->release != NULL) {
		GPtrArray *cats = NULL;
		GPtrArray *checks = NULL;
		GPtrArray *issues = NULL;
		g_autofree gchar *desc = NULL;
		g_autoptr(GString) size = g_string_new (NULL);
		g_autoptr(GError) error = NULL;
		g_autoptr(GString) attr = g_string_new (NULL);

		gfu_main_set_label (self, "label_release_version", fwupd_release_get_version (self->release));

		cats = fwupd_release_get_categories (self->release);
		if (cats->len == 0) {
			gfu_main_set_label (self, "label_release_categories", NULL);
		} else {
			for (guint i = 0; i < cats->len; i++) {
				g_string_append_printf (attr, "%s\n", (const gchar *) g_ptr_array_index (cats, i));
			}
			gfu_main_set_label (self, "label_release_categories", attr->str);
			if (attr->len > 0)
				g_string_truncate (attr, attr->len - 1);
			if (cats->len == 1) {
				gfu_main_set_label_title (self, "label_release_categories", "Category");
			} else {
				gfu_main_set_label_title (self, "label_release_categories", "Categories");
			}
		}

		g_string_set_size (attr, 0);
		checks = fwupd_release_get_checksums (self->release);
		if (checks->len == 0) {
			gfu_main_set_label (self, "label_release_checksum", NULL);
		} else {
			for (guint i = 0; i < checks->len; i++) {
				g_autofree gchar *tmp = gfu_common_checksum_format (g_ptr_array_index (checks, i));
				g_string_append_printf (attr, "%s\n", tmp);
			}
			if (attr->len > 0)
				g_string_truncate (attr, attr->len - 1);
			gfu_main_set_label (self, "label_release_checksum", attr->str);
			gfu_main_set_label_title (self, "label_release_checksum",
						  ngettext ("Checksum", "Checksums", checks->len));
		}

#if FWUPD_CHECK_VERSION(1,3,2)
		issues = fwupd_release_get_issues (self->release);
#endif
		if (issues == NULL || issues->len == 0) {
			gfu_main_set_label (self, "label_release_issues", NULL);
		} else {
			g_autoptr(GString) str = g_string_new (NULL);
			for (guint i = 0; i < issues->len; i++) {
				const gchar *tmp = g_ptr_array_index (issues, i);
				g_string_append_printf (str, "%s\n", tmp);
			}
			if (str->len > 0)
				g_string_truncate (str, str->len - 1);
			gfu_main_set_label (self, "label_release_issues", str->str);
			gfu_main_set_label_title (self, "label_release_checksum",
						  /* TRANSLATORS: e.g. CVEs */
						  ngettext ("Fixed Issue", "Fixed Issues", checks->len));
		}

		gfu_main_set_label (self, "label_release_filename", fwupd_release_get_filename (self->release));
		gfu_main_set_label (self, "label_release_protocol", fwupd_release_get_protocol (self->release));
		gfu_main_set_label (self, "label_release_appstream_id", fwupd_release_get_appstream_id (self->release));
		gfu_main_set_label (self, "label_release_remote_id", fwupd_release_get_remote_id (self->release));
		gfu_main_set_label (self, "label_release_vendor", fwupd_release_get_vendor (self->release));
		gfu_main_set_label (self, "label_release_summary", fwupd_release_get_summary (self->release));

		g_string_set_size (attr, 0);
		desc = gfu_common_xml_to_text (fwupd_release_get_description (self->release), &error);
		if (desc == NULL) {
			g_debug ("failed to get release description for version %s: %s", fwupd_release_get_version (self->release), error->message);
			gfu_main_set_label (self, "label_release_description", NULL);
		} else {
			g_string_append (attr, desc);
			if (attr->len > 0)
				g_string_truncate (attr, attr->len - 1);
			gfu_main_set_label (self, "label_release_description", attr->str);
		}

		gfu_main_set_label (self, "label_release_size", g_format_size(fwupd_release_get_size (self->release)));

		gfu_main_set_label (self, "label_release_license", fwupd_release_get_license (self->release));

		gfu_main_set_label (self, "label_release_flags",
				    gfu_common_release_flags_to_strings (fwupd_release_get_flags (self->release)));

		gfu_main_set_label (self, "label_release_install_duration",
				    gfu_common_seconds_to_string (fwupd_release_get_install_duration (self->release)));

		gfu_main_set_label (self, "label_release_update_message",
				    fwupd_release_get_update_message (self->release));

		/* install button */
		w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_install"));

		if (self->device != NULL) {
			gtk_widget_set_sensitive (w, TRUE);
			if (self->release != NULL && self->releases != NULL) {
				if (fwupd_release_has_flag (self->release, FWUPD_RELEASE_FLAG_IS_UPGRADE)) {
					/* TRANSLATORS: upgrading the firmware */
					gtk_button_set_label (GTK_BUTTON (w), _("Upgrade"));
				} else if (fwupd_release_has_flag (self->release, FWUPD_RELEASE_FLAG_IS_DOWNGRADE)) {
					/* TRANSLATORS: downgrading the firmware */
					gtk_button_set_label (GTK_BUTTON (w), _("Downgrade"));
				} else {
					/* TRANSLATORS: installing the same firmware that is currently installed */
					gtk_button_set_label (GTK_BUTTON (w), _("Reinstall"));
				}
			}
		} else {
			gtk_widget_set_sensitive (w, FALSE);
			/* TRANSLATORS: general install button in the event of an error; not clickable */
			gtk_button_set_label (GTK_BUTTON (w), _("Install"));
		}

	}

	/* refresh button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "menu_button"));
	gtk_widget_set_visible (w, self->mode != GFU_MAIN_MODE_RELEASE);

	/* back button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
	gtk_widget_set_visible (w, self->mode == GFU_MAIN_MODE_RELEASE);

	/* unlock button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_unlock"));
	gtk_widget_set_visible (w, self->device != NULL &&
				   fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_LOCKED));

	/* verify button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_verify"));
#if FWUPD_CHECK_VERSION(1,3,3)
	gtk_widget_set_visible (w, self->device != NULL &&
				   !verification_matched &&
				   fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE));
#else
	gtk_widget_set_visible (w, verification_matched);
#endif

	/* verify update button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_verify_update"));
#if FWUPD_CHECK_VERSION(1,3,3)
	gtk_widget_set_visible (w, self->device != NULL &&
				   !verification_matched &&
				   fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_CAN_VERIFY));
#else
	gtk_widget_set_visible (w, verification_matched);
#endif
	/* releases button */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_releases"));
	if (self->releases == NULL || self->releases->len == 0) {
		gtk_widget_set_visible (w, FALSE);
	} else {
		gtk_widget_set_visible (w, TRUE);
	}
}

static void
gfu_main_remove_row (GtkWidget *row, GtkListBox *w)
{
	gtk_container_remove (GTK_CONTAINER (w), row);
}

/* updating devices while the application is open */

static void
gfu_main_device_added_cb (FwupdClient *client, FwupdDevice *device, GfuMain *self)
{
	GtkWidget *w;
	GtkWidget *l;

	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "listbox_main"));

	/* ignore if device can't be updated */
	if (!fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE))
		return;
	/* create and add new row for device */
	l = gfu_device_row_new (device);
	gtk_widget_set_visible (l, TRUE);
	gtk_list_box_insert (GTK_LIST_BOX (w), l, -1);

	/* if no row is selected (list was previously empty), select the first one */
	if (gtk_list_box_get_selected_row (GTK_LIST_BOX (w)) == NULL)
		gtk_list_box_select_row (GTK_LIST_BOX (w), GTK_LIST_BOX_ROW (l));
}

static void
gfu_main_remove_device_from_list (GtkWidget *row, GfuDeviceRowHelper *rowcheck)
{
	FwupdDevice *result = rowcheck->row;
	GtkListBox *w = rowcheck->list;

	if (!FWUPD_IS_DEVICE (result))
		return;

	if (fwupd_device_compare (gfu_device_row_get_device (GFU_DEVICE_ROW (row)), result) == 0) {
		/* if row to be removed is the currently selected row, select the first row of the list */
		GtkListBoxRow *sel = gtk_list_box_get_selected_row (GTK_LIST_BOX (w));
		if (fwupd_device_compare (gfu_device_row_get_device (GFU_DEVICE_ROW (sel)), result) == 0) {
			GtkListBoxRow *l = gtk_list_box_get_row_at_index (GTK_LIST_BOX (w), 0);
			if (l != NULL)
				gtk_list_box_select_row (GTK_LIST_BOX (w), l);
		}
		/* remove row corresponding to the device */
		gtk_container_remove (GTK_CONTAINER (w), row);
	}
}

static void
gfu_main_device_removed_cb (FwupdClient *client, FwupdDevice *device, GfuMain *self)
{
	GtkContainer *w;
	g_autofree GfuDeviceRowHelper *rowcheck = g_new0 (GfuDeviceRowHelper, 1);

	w = GTK_CONTAINER (gtk_builder_get_object (self->builder, "listbox_main"));

	/* package data */
	rowcheck->list = GTK_LIST_BOX (w);
	rowcheck->row = device;

	gtk_container_foreach (w, (GtkCallback) gfu_main_remove_device_from_list, rowcheck);
}

static void
gfu_main_reboot_shutdown_prompt (GfuMain *self)
{
	g_autoptr(GError) error = NULL;
	guint64 flags = fwupd_device_get_flags (self->device);

	/* if successful, prompt for reboot */
	// FIXME: handle with device::changed instead of removing

	if (flags & FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		/* shutdown prompt */
		GtkWindow *window;
		GtkWidget *dialog;

		window = GTK_WINDOW (gtk_builder_get_object (self->builder, "dialog_main"));
		dialog = gtk_message_dialog_new (window,
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_YES_NO,
						 /* TRANSLATORS: prompting a shutdown to the user */
						 "%s", _("An update requires the system to shutdown to complete."));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "Shutdown now?");
		switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
		case GTK_RESPONSE_YES:
			if (!gfu_common_system_shutdown (&error)) {
				/* remove device from list until system is rebooted */
				GtkContainer *w;
				g_autofree GfuDeviceRowHelper *rowcheck = g_new0 (GfuDeviceRowHelper, 1);
				w = GTK_CONTAINER (gtk_builder_get_object (self->builder, "listbox_main"));

				/* package data */
				rowcheck->list = GTK_LIST_BOX (w);
				rowcheck->row = self->device;

				/* remove corresponding device */
				gtk_container_foreach (w, (GtkCallback) gfu_main_remove_device_from_list, rowcheck);

				g_debug ("Failed to shutdown device: %s\n", error->message);

				/* TRANSLATORS: error in shutting down the system */
				gfu_main_error_dialog (self, _("Failed to shutdown device"), _("A manual shutdown is required."));
			}
			break;
		case GTK_RESPONSE_NO:
			break;
		default:
			break;
		}

		gtk_widget_destroy (dialog);
	}
	if (flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		/* shutdown prompt */
		GtkWindow *window;
		GtkWidget *dialog;

		window = GTK_WINDOW (gtk_builder_get_object (self->builder, "dialog_main"));
		dialog = gtk_message_dialog_new (window,
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_YES_NO,
						 /* TRANSLATORS: prompting a reboot to the user */
						 "%s", _("An update requires a reboot to complete."));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  "Restart now?");
		switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
		case GTK_RESPONSE_YES:
			if (!gfu_common_system_reboot (&error)) {
				/* remove device from list until system is rebooted */
				GtkContainer *w;
				g_autofree GfuDeviceRowHelper *rowcheck = g_new0 (GfuDeviceRowHelper, 1);
				w = GTK_CONTAINER (gtk_builder_get_object (self->builder, "listbox_main"));

				/* package data */
				rowcheck->list = GTK_LIST_BOX (w);
				rowcheck->row = self->device;

				/* remove corresponding device */
				gtk_container_foreach (w, (GtkCallback) gfu_main_remove_device_from_list, rowcheck);

				g_debug ("Failed to reboot device: %s\n", error->message);

				/* TRANSLATORS: error in rebooting down the system */
				gfu_main_error_dialog (self, _("Failed to reboot device"), _("A manual reboot is required."));
			}
			break;
		case GTK_RESPONSE_NO:
			break;
		default:
			break;
		}

		gtk_widget_destroy (dialog);
	}
}

#if 0
static void
gfu_cancel_clicked_cb (GtkWidget *widget, GfuMain *self)
{
	g_cancellable_cancel (self->cancellable);
}
#endif

/* async getter functions */

static void
gfu_main_update_remotes_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GfuMain *self = (GfuMain *) user_data;
	GtkWidget *w;
	gboolean disabled_lvfs_remote = FALSE;
	gboolean enabled_any_download_remote = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) val = g_dbus_proxy_call_finish (self->proxy, res, &error);
	g_autoptr(GPtrArray) remotes = NULL;

	if (val == NULL) {
		gfu_main_error_dialog (self, _("Failed to load list of remotes"), error->message);
		return;
	}
	remotes = fwupd_remote_array_from_variant (val);
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		g_debug ("%s is %s", fwupd_remote_get_id (remote),
			 fwupd_remote_get_enabled (remote) ? "enabled" : "disabled");

		/* if another repository is turned on providing metadata */
		if (fwupd_remote_get_enabled (remote)) {
			if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DOWNLOAD)
				enabled_any_download_remote = TRUE;

		/* lvfs is present and disabled */
		} else {
			if (g_strcmp0 (fwupd_remote_get_id (remote), "lvfs") == 0)
				disabled_lvfs_remote = TRUE;
		}
	}

	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_enable_lvfs"));
	gtk_info_bar_set_revealed (GTK_INFO_BAR (w),
				   disabled_lvfs_remote && !enabled_any_download_remote);
}

static void
gfu_main_update_devices_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GtkWidget *w;
	GtkWidget *l = NULL;
	GfuMain *self = (GfuMain *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(GVariant) tmp = g_dbus_proxy_call_finish (self->proxy, res, &error);

	if (tmp == NULL) {
		gfu_main_error_dialog (self, _("Failed to load device list"), error->message);
		return;
	}

	devices = fwupd_device_array_from_variant (tmp);

	/* create a row in the listbox for each updatable device */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "listbox_main"));
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index (devices, i);
		/* skip devices that can't be updated */
		if (!fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_LOCKED)) {
			g_debug ("ignoring non-updatable device: %s", fwupd_device_get_name (device));
			continue;
		}
		l = gfu_device_row_new (device);
		gtk_widget_set_visible (l, TRUE);
		gtk_list_box_insert (GTK_LIST_BOX (w), l, -1);
	}

	/* if no row is selected and there are rows in the list, select the first one */
	if (gtk_list_box_get_selected_row (GTK_LIST_BOX (w)) == NULL && l != NULL) {
		gtk_list_box_select_row (GTK_LIST_BOX (w),
					 gtk_list_box_get_row_at_index (GTK_LIST_BOX (w), 0));
	}
}

static void
gfu_main_update_releases_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GtkWidget *w;
	GfuMain *self = (GfuMain *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) tmp = g_dbus_proxy_call_finish (self->proxy, res, &error);
	if (tmp == NULL) {
		/* No firmware found for this devices */
		g_debug ("ignoring: %s", error->message);
		return;
	}
	self->releases = fwupd_release_array_from_variant (tmp);

	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "listbox_firmware"));
	gfu_main_container_remove_all (GTK_CONTAINER (w));
	for (guint i = 0; i < self->releases->len; i++) {
		FwupdRelease *release = g_ptr_array_index (self->releases, i);
		GtkWidget *l = gfu_release_row_new (release);
		gtk_widget_set_visible (l, TRUE);
		gtk_list_box_insert (GTK_LIST_BOX (w), l, -1);
	}

	gfu_main_refresh_ui (self);
}

/* installation code, some from fwupd-client */

static void
gfu_main_set_install_loading_label (GfuMain *self, gchar *text)
{
	GtkLabel *label = GTK_LABEL (gtk_builder_get_object (self->builder, "install_spinner_device_label"));
	gtk_label_set_label (label, text);
	label = GTK_LABEL (gtk_builder_get_object (self->builder, "install_spinner_release_label"));
	gtk_label_set_label (label, text);
}

static void
gfu_main_set_install_status_label (GfuMain *self, gchar *text)
{
	GtkLabel *label = GTK_LABEL (gtk_builder_get_object (self->builder, "install_spinner_device_status_label"));
	gtk_label_set_label (label, text);
	label = GTK_LABEL (gtk_builder_get_object (self->builder, "install_spinner_release_status_label"));
	gtk_label_set_label (label, text);
	g_debug ("Updated status label: %s", text);
}

static void
gfu_main_show_install_loading (GfuMain *self, gboolean show)
{
	GtkWidget *w = GTK_WIDGET (gtk_builder_get_object (self->builder, "dialog_main"));
	gtk_widget_set_sensitive (w, !show);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "install_spinner_device"));
	gtk_widget_set_visible (w, show);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "install_spinner_release"));
	gtk_widget_set_visible (w, show);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_release_metadata"));
	gtk_widget_set_visible (w, !show);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_device_metadata"));
	gtk_widget_set_visible (w, !show);
}

static void
gfu_main_download_chunk_cb (SoupMessage *msg, SoupBuffer *chunk, gpointer user_data)
{
	guint percentage;
	goffset header_size;
	goffset body_length;
	GfuMain *self = (GfuMain *) user_data;

	/* if it's returning "Found" or an error, ignore the percentage */
	if (msg->status_code != SOUP_STATUS_OK) {
		g_debug ("ignoring status code %u (%s)",
			 msg->status_code, msg->reason_phrase);
		return;
	}

	/* get data */
	body_length = msg->response_body->length;
	header_size = soup_message_headers_get_content_length (msg->response_headers);

	/* size is not known */
	if (header_size < body_length)
		return;

	/* calculate percentage */
	percentage = (guint) ((100 * body_length) / header_size);
	g_debug ("progress: %u%%", percentage);
	gfu_main_set_install_loading_label (self, g_strdup_printf ("%s (%u%%)",
								   _("Downloading"),
								   percentage));
}

static gboolean
gfu_main_download_file (GfuMain *self,
			SoupURI *uri,
			const gchar *fn,
			const gchar *checksum_expected,
			GError **error)
{
	GChecksumType checksum_type;
	guint status_code;
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* check if the file already exists with the right checksum */
	checksum_type = fwupd_checksum_guess_kind (checksum_expected);
	if (gfu_common_file_exists_with_checksum (fn, checksum_expected, checksum_type)) {
		g_debug ("skipping download as file already exists");
		gfu_main_set_install_loading_label (self, _("File already downloaded..."));
		return TRUE;
	}

	/* set up networking */
	if (self->soup_session == NULL) {
		self->soup_session = gfu_common_setup_networking (error);
		if (self->soup_session == NULL)
			return FALSE;
	}

	/* download data */
	uri_str = soup_uri_to_string (uri, FALSE);
	g_debug ("downloading %s to %s", uri_str, fn);
	gfu_main_set_install_loading_label (self, _("Downloading file..."));
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, uri);
	if (msg == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to parse URI %s", uri_str);
		return FALSE;
	}
	if (g_str_has_suffix (uri_str, ".asc") ||
	    g_str_has_suffix (uri_str, ".p7b") ||
	    g_str_has_suffix (uri_str, ".p7c")) {
		/* TRANSLATORS: downloading new signing file */
		g_debug ("%s %s\n", _("Fetching signature"), uri_str);
		gfu_main_set_install_loading_label (self, _("Fetching signature..."));
	} else if (g_str_has_suffix (uri_str, ".gz")) {
		/* TRANSLATORS: downloading new metadata file */
		g_debug ("%s %s\n", _("Fetching metadata"), uri_str);
		gfu_main_set_install_loading_label (self, _("Fetching metadata..."));
	} else if (g_str_has_suffix (uri_str, ".cab")) {
		/* TRANSLATORS: downloading new firmware file */
		g_debug ("%s %s\n", _("Fetching firmware"), uri_str);
		gfu_main_set_install_loading_label (self, _("Fetching firmware..."));
	} else {
		/* TRANSLATORS: downloading unknown file */
		g_debug ("%s %s\n", _("Fetching file"), uri_str);
		gfu_main_set_install_loading_label (self, _("Fetching file..."));
	}
	g_signal_connect (msg, "got-chunk",
			  G_CALLBACK (gfu_main_download_chunk_cb), self);
	status_code = soup_session_send_message (self->soup_session, msg);
	g_debug ("\n");
	if (status_code == 429) {
		g_autofree gchar *str = g_strndup (msg->response_body->data,
						   msg->response_body->length);
		if (g_strcmp0 (str, "Too Many Requests") == 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     /* TRANSLATORS: the server is rate-limiting downloads */
				     "%s", _("Failed to download due to server limit"));
			return FALSE;
		}
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     _("Failed to download due to server limit: %s"), str);
		return FALSE;
	}
	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     _("Failed to download %s: %s"),
			     uri_str, soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* verify checksum */
	if (checksum_expected != NULL) {
		checksum_actual = g_compute_checksum_for_data (checksum_type,
							       (guchar *) msg->response_body->data,
							       (gsize) msg->response_body->length);
		if (g_strcmp0 (checksum_expected, checksum_actual) != 0) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     _("Checksum invalid, expected %s got %s"),
				     checksum_expected, checksum_actual);
			return FALSE;
		}
	}

	/* save file */
	if (!g_file_set_contents (fn,
				  msg->response_body->data,
				  msg->response_body->length,
				  &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     _("Failed to save file: %s"),
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
gfu_main_download_metadata_for_remote (GfuMain *self,
				       FwupdRemote *remote,
				       GError **error)
{
	g_autofree gchar *basename_asc = NULL;
	g_autofree gchar *basename_id_asc = NULL;
	g_autofree gchar *basename_id = NULL;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *filename_asc = NULL;
	g_autoptr(SoupURI) uri = NULL;
	g_autoptr(SoupURI) uri_sig = NULL;

	/* generate some plausible local filenames */
	basename = g_path_get_basename (fwupd_remote_get_filename_cache (remote));
	basename_id = g_strdup_printf ("%s-%s", fwupd_remote_get_id (remote), basename);

	/* download the metadata */
	filename = gfu_get_user_cache_path (basename_id);
	gfu_main_set_install_loading_label (self, _("Creating cache path..."));
	if (!gfu_common_mkdir_parent (filename, error))
		return FALSE;
	uri = soup_uri_new (fwupd_remote_get_metadata_uri (remote));
	gfu_main_set_install_loading_label (self, _("Preparing to download file..."));
	if (!gfu_main_download_file (self, uri, filename, NULL, error))
		return FALSE;

	/* download the signature */
	basename_asc = g_path_get_basename (fwupd_remote_get_filename_cache_sig (remote));
	basename_id_asc = g_strdup_printf ("%s-%s", fwupd_remote_get_id (remote), basename_asc);
	filename_asc = gfu_get_user_cache_path (basename_id_asc);
	uri_sig = soup_uri_new (fwupd_remote_get_metadata_uri_sig (remote));
	gfu_main_set_install_loading_label (self, _("Preparing to download file..."));
	if (!gfu_main_download_file (self, uri_sig, filename_asc, NULL, error))
		return FALSE;

	/* send all this to fwupd */
	return fwupd_client_update_metadata (self->client,
					     fwupd_remote_get_id (remote),
					     filename,
					     filename_asc,
					     NULL, error);
}

static void
gfu_main_enable_lvfs_cb (GtkWidget *widget, GfuMain *self)
{
	GtkWidget *w;
	g_autoptr(FwupdRemote) remote = NULL;
	g_autoptr(GError) error = NULL;

	/* hide notification */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_enable_lvfs"));
	gtk_info_bar_set_revealed (GTK_INFO_BAR (w), FALSE);

	/* enable remote */
	gfu_main_show_install_loading (self, TRUE);
	if (!fwupd_client_modify_remote (self->client, "lvfs", "Enabled", "true", NULL, &error)) {
		gfu_main_error_dialog (self, _("Failed to enable LVFS"), error->message);
		gfu_main_show_install_loading (self, FALSE);
		return;
	}

	/* refresh the newly-enabled remote */
	remote = fwupd_client_get_remote_by_id (self->client, "lvfs", NULL, &error);
	if (remote == NULL) {
		gfu_main_error_dialog (self, _("Failed to find LVFS"), error->message);
		gfu_main_show_install_loading (self, FALSE);
		return;
	}
	if (!gfu_main_download_metadata_for_remote (self, remote, &error)) {
		gfu_main_error_dialog (self, _("Failed to download metadata for LVFS"), error->message);
		gfu_main_show_install_loading (self, FALSE);
		return;
	}
	gfu_main_show_install_loading (self, FALSE);
}

static gboolean
gfu_main_download_metadata (GfuMain *self, GError **error)
{
	g_autoptr(GPtrArray) remotes = NULL;

	/* begin downloading, show loading animation */
	gfu_main_show_install_loading (self, TRUE);

	remotes = fwupd_client_get_remotes (self->client, NULL, error);
	if (remotes == NULL) {
		gfu_main_show_install_loading (self, FALSE);
		return FALSE;
	}
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (!fwupd_remote_get_enabled (remote))
			continue;
		if (fwupd_remote_get_kind (remote) != FWUPD_REMOTE_KIND_DOWNLOAD)
			continue;
		if (!gfu_main_download_metadata_for_remote (self, remote, error)) {
			gfu_main_show_install_loading (self, FALSE);
			return FALSE;
		}
	}

	gfu_main_show_install_loading (self, FALSE);
	return TRUE;
}

static void
gfu_main_activate_refresh_metadata (GSimpleAction *simple, GVariant *parameter, gpointer user_data)
{
	GfuMain *self = (GfuMain *) user_data;
	g_autoptr(GError) error = NULL;
	if (!gfu_main_download_metadata (self, &error))
		gfu_main_error_dialog (self, _("Failed to download metadata"), error->message);
}

static gboolean
gfu_main_install_release_to_device (GfuMain *self,
				    FwupdDevice *dev,
				    FwupdRelease *rel,
				    GError **error)
{
	GPtrArray *checksums;
	const gchar *remote_id;
	const gchar *uri_tmp;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri_str = NULL;
	g_autofree gchar *install_str = NULL;
	g_autoptr(SoupURI) uri = NULL;

	/* work out what remote-specific URI fields this should use */
	uri_tmp = fwupd_release_get_uri (rel);
	remote_id = fwupd_release_get_remote_id (rel);
	if (remote_id != NULL) {
		g_autoptr(FwupdRemote) remote = NULL;
		remote = fwupd_client_get_remote_by_id (self->client,
							remote_id,
							NULL,
							error);
		if (remote == NULL)
			return FALSE;

		/* local and directory remotes have the firmware already */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_LOCAL) {
			const gchar *fn_cache = fwupd_remote_get_filename_cache (remote);
			g_autofree gchar *path = g_path_get_dirname (fn_cache);

			fn = g_build_filename (path, uri_tmp, NULL);
		} else if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DIRECTORY) {
			fn = g_strdup (uri_tmp + 7);
		}
		/* install with flags chosen by the user */
		if (fn != NULL) {
			return fwupd_client_install (self->client,
						     fwupd_device_get_id (dev),
						     fn, self->flags, NULL, error);
		}

		uri_str = fwupd_remote_build_firmware_uri (remote, uri_tmp, error);
		if (uri_str == NULL)
			return FALSE;
	} else {
		uri_str = g_strdup (uri_tmp);
	}

	/* download file */
	g_debug ("Downloading %s for %s...\n",
		 fwupd_release_get_version (rel),
		 fwupd_device_get_name (dev));
	gfu_main_set_install_loading_label (self, _("Preparing to download file..."));
	/* place in gfu cache directory */
	fn = gfu_get_user_cache_path (uri_str);
	/* TRANSLATORS: creating directory for the firmware download */
	gfu_main_set_install_loading_label (self, _("Creating cache path..."));
	if (!gfu_common_mkdir_parent (fn, error))
		return FALSE;
	checksums = fwupd_release_get_checksums (rel);
	uri = soup_uri_new (uri_str);
	if (!gfu_main_download_file (self, uri, fn,
				    fwupd_checksum_get_best (checksums),
				    error))
		return FALSE;
	/* if the device specifies ONLY_OFFLINE automatically set this flag */
	if (fwupd_device_has_flag (dev, FWUPD_DEVICE_FLAG_ONLY_OFFLINE))
		self->flags |= FWUPD_INSTALL_FLAG_OFFLINE;
	install_str = gfu_operation_to_string (self->current_operation, dev);
	gfu_main_set_install_loading_label (self, install_str);
	g_timer_start (self->time_elapsed);
        return fwupd_client_install (self->client,
				     fwupd_device_get_id (dev), fn,
				     self->flags, NULL, error);
}

/* used to retrieve the current device post-install */
typedef struct {
	GfuMain *self;
	gchar *device_id;
} GfuPostInstallHelper;

static void
gfu_main_update_devices_post_install_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GtkContainer *w;
	GtkWidget *l = NULL;
	GPtrArray *devices;
	GfuPostInstallHelper *helper = (GfuPostInstallHelper*)user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) tmp = g_dbus_proxy_call_finish (helper->self->proxy, res, &error);

	/* clear out device list */
	w = GTK_CONTAINER (gtk_builder_get_object (helper->self->builder, "listbox_main"));
	gtk_list_box_unselect_all (GTK_LIST_BOX (w));
	gtk_container_foreach (w, (GtkCallback) gfu_main_remove_row, w);

	if (tmp == NULL) {
		gfu_main_error_dialog (helper->self, _("Failed to load device list"), error->message);
		return;
	}

	devices = fwupd_device_array_from_variant (tmp);

	/* create a row in the listbox for each updatable device */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index (devices, i);
		/* skip devices that can't be updated */
		if (!fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE) &&
		    !fwupd_device_has_flag (device, FWUPD_DEVICE_FLAG_LOCKED)) {
			g_debug ("ignoring non-updatable device: %s", fwupd_device_get_name (device));
			continue;
		}
		l = gfu_device_row_new (device);
		gtk_widget_set_visible (l, TRUE);
		gtk_list_box_insert (GTK_LIST_BOX (w), l, -1);

		/* update our current device now that new firmware has been installed */
		if (g_strcmp0 (helper->device_id, fwupd_device_get_id (device)) == 0) {
			helper->self->device = device;
			gtk_list_box_select_row (GTK_LIST_BOX (w), GTK_LIST_BOX_ROW (l));
		}
	}

	/* reboot or shutdown if necessary (UEFI update) */
	gfu_main_reboot_shutdown_prompt (helper->self);

	/* if no row is selected and there are rows in the list, select the first one */
	if (gtk_list_box_get_selected_row (GTK_LIST_BOX (w)) == NULL && l != NULL) {
		gtk_list_box_select_row (GTK_LIST_BOX (w),
					 gtk_list_box_get_row_at_index (GTK_LIST_BOX (w), 0));
	}

	/* update release list */
	g_dbus_proxy_call (helper->self->proxy,
			   "GetReleases",
			   g_variant_new ("(s)", helper->device_id),
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   helper->self->cancellable,
			   (GAsyncReadyCallback) gfu_main_update_releases_cb,
			   helper->self);
}

static void
gfu_main_release_install_file_cb (GtkWidget *widget, GfuMain *self)
{
	GtkWidget *window;
	GtkWidget *dialog;
	const gchar *title_string = NULL;
	g_autoptr(GError) error = NULL;
	gboolean upgrade = fwupd_release_has_flag (self->release, FWUPD_RELEASE_FLAG_IS_UPGRADE);
	gboolean downgrade = fwupd_release_has_flag (self->release, FWUPD_RELEASE_FLAG_IS_DOWNGRADE);
	gboolean reinstall = !downgrade && !upgrade;

	/* make sure user wants to install file to device */
	if (reinstall) {
		/* TRANSLATORS: %1 is device name, %2 is the version */
		title_string = _("Reinstall %s firmware version %s");
	} else if (upgrade) {
		/* TRANSLATORS: %1 is device name, %2 is the version */
		title_string = _("Upgrade %s firmware version %s");
	} else if (downgrade) {
		/* TRANSLATORS: %1 is device name, %2 is the version */
		title_string = _("Downgrade %s firmware version %s");
	} else {
		/* TRANSLATORS: %1 is device name, %2 is the version */
		title_string = _("Install %s firmware version %s");
	}
	window = GTK_WIDGET (gtk_builder_get_object (self->builder, "dialog_main"));
	dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					 GTK_DIALOG_USE_HEADER_BAR,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_NONE,
					 title_string,
					 fwupd_device_get_name (self->device),
					 fwupd_release_get_version (self->release));
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);

	if (reinstall) {
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("Reinstall"), GTK_RESPONSE_OK);
		self->current_operation = GFU_OPERATION_INSTALL;
		self->flags |= FWUPD_INSTALL_FLAG_ALLOW_REINSTALL;
	/* upgrade or downgrade */
	} else {
		if (upgrade) {
			self->current_operation = GFU_OPERATION_UPDATE;
			gtk_dialog_add_button (GTK_DIALOG (dialog), _("Upgrade"), GTK_RESPONSE_OK);
		} else if (downgrade) {
			self->flags |= FWUPD_INSTALL_FLAG_ALLOW_OLDER;
			self->current_operation = GFU_OPERATION_DOWNGRADE;
			gtk_dialog_add_button (GTK_DIALOG (dialog), _("Downgrade"), GTK_RESPONSE_OK);
		}
	}
#if FWUPD_CHECK_VERSION(1,3,3)
	if (fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE))
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("The device will remain usable for the duration of the update"));
	else
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("The device will be unusable while the update is installing"));
#else
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						 reinstall? _("Reinstall this firmware file?"): _("Install this firmware file?"));
#endif

	/* handle dialog response */
	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_OK:
	{
		/* keep track of which device is being installed upon */
		g_autoptr(GString) device_id = g_string_new (NULL);
		g_string_assign (device_id, fwupd_device_get_id (self->device));

		gtk_widget_destroy (dialog);

		/* begin installing, show loading animation */
		gfu_main_show_install_loading (self, TRUE);

		if (!gfu_main_install_release_to_device (self, self->device, self->release, &error)) {
			gfu_main_show_install_loading (self, FALSE);
			gfu_main_error_dialog (self, _("Failed to install firmware release"), error->message);
		} else {
			/* create helper struct */
			GfuPostInstallHelper *helper = g_new0 (GfuPostInstallHelper, 1);
			guint64 flags = fwupd_device_get_flags (self->device);
			helper->self = self;
			helper->device_id = device_id->str;

			/* update device list */
			g_dbus_proxy_call (self->proxy,
					   "GetDevices",
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   self->cancellable,
					   (GAsyncReadyCallback) gfu_main_update_devices_post_install_cb,
					   helper);

			gfu_main_show_install_loading (self, FALSE);

			g_debug ("Installation complete.\n");
			// FIXME: device-changed signal

			if (flags & FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN ||
			    flags & FWUPD_DEVICE_FLAG_NEEDS_REBOOT)
				break;

			dialog = gtk_message_dialog_new (GTK_WINDOW (window),
							GTK_DIALOG_MODAL,
							GTK_MESSAGE_INFO,
							GTK_BUTTONS_OK,
							/* TRANSLATORS: inform the user that the installation was successful onto the device */
							"%s", _("Installation successful"));
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
								_("Installed firmware version %s on %s"),
								fwupd_release_get_version (self->release),
								fwupd_device_get_name (self->device));

			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
		self->flags = FWUPD_INSTALL_FLAG_NONE;
		break;
	}
	case GTK_RESPONSE_CANCEL:
		gtk_widget_destroy (dialog);
		gfu_main_show_install_loading (self, FALSE);
		break;
	default:
		gtk_widget_destroy (dialog);
		gfu_main_show_install_loading (self, FALSE);
		break;
	}
}

static void
gfu_main_device_releases_cb (GtkWidget *widget, GfuMain *self)
{
	self->mode = GFU_MAIN_MODE_RELEASE;
	gfu_main_refresh_ui (self);
}

static void
gfu_main_button_back_cb (GtkWidget *widget, GfuMain *self)
{
	self->mode = GFU_MAIN_MODE_DEVICE;
	gfu_main_refresh_ui (self);
}

static void
gfu_main_device_verify_cb (GtkWidget *widget, GfuMain *self)
{
	GtkWidget *dialog;
	GtkWidget *window = GTK_WIDGET (gtk_builder_get_object (self->builder, "dialog_main"));
	g_autoptr(GError) error = NULL;

	dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 "%s",
					_("Verify firmware checksums?"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						_("The device may be unusable during this action"));
	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_YES:
		gtk_widget_destroy (dialog);
		g_clear_error (&error);
		if (!fwupd_client_verify (self->client,
					  fwupd_device_get_id (self->device),
					  self->cancellable,
					  &error)) {
			/* TRANSLATORS: verify means checking the actual checksum of the firmware */
			gfu_main_error_dialog (self, _("Failed to verify firmware"), error->message);
		} else {
			dialog = gtk_message_dialog_new (GTK_WINDOW (window),
							 GTK_DIALOG_MODAL,
							 GTK_MESSAGE_INFO,
							 GTK_BUTTONS_OK,
							 /* TRANSLATORS: inform the user that the firmware verification was successful */
							 "%s", _("Verification succeeded"));
			gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
								/* TRANSLATORS: firmware is cryptographically identical */
								_("%s firmware checksums matched"),
								fwupd_device_get_name (self->device));

			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);

		}
		return;
	default:
		gtk_widget_destroy (dialog);
	}
}

static void
gfu_main_device_verify_update_cb (GtkWidget *widget, GfuMain *self)
{
	GtkWidget *dialog;
	GtkWidget *window = GTK_WIDGET (gtk_builder_get_object (self->builder, "dialog_main"));
	g_autoptr(GError) error = NULL;

	dialog = gtk_message_dialog_new (GTK_WINDOW (window),
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 "%s",
					_("Update cryptographic hash"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						_("Record current device cryptographic hashes as verified?"));
	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
	case GTK_RESPONSE_YES:
		gtk_widget_destroy (dialog);
		g_clear_error (&error);
		if (!fwupd_client_verify_update (self->client,
						 fwupd_device_get_id (self->device),
						 self->cancellable,
						 &error)) {
			/* TRANSLATORS: verify means checking the actual checksum of the firmware */
			gfu_main_error_dialog (self, _("Failed to update checksums"), error->message);
		}
		gfu_main_refresh_ui (self);
		return;
	default:
		gtk_widget_destroy (dialog);
	}
}

static void
gfu_main_update_title (GfuMain *self)
{
	GtkWidget *w;
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "header"));
	gtk_header_bar_set_subtitle (GTK_HEADER_BAR (w), NULL);
}

static void
gfu_main_about_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GfuMain *self = (GfuMain *) user_data;
	GList *windows;
	GtkWindow *parent = NULL;
	const gchar *authors[] = { "Richard Hughes", "Andrew Schwenn", "Sean Rhodes", NULL };
	const gchar *copyright = "Copyright \xc2\xa9 2020 Richard Hughes";

	windows = gtk_application_get_windows (GTK_APPLICATION (self->application));
	if (windows)
		parent = windows->data;

	gtk_show_about_dialog (parent,
			       /* TRANSLATORS: the title of the about window */
			       "title", _("About Firmware Update"),
			       /* TRANSLATORS: the application name for the about UI */
			       "program-name", _("Firmware Update"),
			       "authors", authors,
			       /* TRANSLATORS: one-line description for the app */
			       "comments", _("Manage firmware on devices"),
			       "copyright", copyright,
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "logo-icon-name", "computer",
			       /* TRANSLATORS: you can put your name here :) */
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

static void
gfu_main_quit_activated_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	GfuMain *self = (GfuMain *) user_data;
	g_application_quit (G_APPLICATION (self->application));
}

static GActionEntry actions[] = {
	{ "about",	gfu_main_about_activated_cb, NULL, NULL, NULL },
	{ "refresh",	gfu_main_activate_refresh_metadata, NULL, NULL, NULL },
	{ "quit",	gfu_main_quit_activated_cb, NULL, NULL, NULL }
};

static void
gfu_main_release_row_selected_cb (GtkListBox *box, GtkListBoxRow *row, GfuMain *self)
{
	FwupdRelease *release;

	/* Ignore if not in the "release" view */
	if (!GFU_IS_RELEASE_ROW (GFU_RELEASE_ROW (row)))
		return;

	release = gfu_release_row_get_release (GFU_RELEASE_ROW (row));
	g_set_object (&self->release, release);
	gfu_main_refresh_ui (self);
}

static void
gfu_main_device_row_selected_cb (GtkListBox *box, GtkListBoxRow *row, GfuMain *self)
{
	FwupdDevice *device;
	g_autoptr(GError) error = NULL;

	if (row == NULL)
		return;

	device = gfu_device_row_get_device (GFU_DEVICE_ROW (row));

	self->mode = GFU_MAIN_MODE_DEVICE;
	g_clear_pointer (&self->releases, g_ptr_array_unref);
	g_set_object (&self->device, device);

	/* async call for releases */
	if (fwupd_device_has_flag (self->device, FWUPD_DEVICE_FLAG_UPDATABLE)) {
		g_dbus_proxy_call (self->proxy,
				   "GetReleases",
				   g_variant_new ("(s)", fwupd_device_get_id (self->device)),
				   G_DBUS_CALL_FLAGS_NONE,
				   -1,
				   self->cancellable,
				   (GAsyncReadyCallback) gfu_main_update_releases_cb,
				   self);
	}

	gfu_main_refresh_ui (self);
}

static gboolean
gfu_main_estimate_ready (GfuMain *self, guint percentage)
{
	gdouble old;
	gdouble elapsed;

	if (percentage == 0 || percentage == 100)
		return FALSE;

	old = self->last_estimate;
	elapsed = g_timer_elapsed (self->time_elapsed, NULL);
	self->last_estimate = elapsed / percentage * (100 - percentage);

	/* estimate is ready if we have decreased */
	return old > self->last_estimate;
}

static gchar *
gfu_main_time_remaining_str (GfuMain *self)
{
	/* less than 5 seconds remaining */
	if (self->last_estimate < 5)
		return NULL;

	/* less than 60 seconds remaining */
	if (self->last_estimate < 60) {
		/* TRANSLATORS: time remaining for completing firmware flash */
		return g_strdup (_("Less than one minute remaining"));
	}

	/* more than a minute */
	return g_strdup_printf (ngettext ("%.0f minute remaining",
					  "%.0f minutes remaining",
					  self->last_estimate / 60),
					  self->last_estimate / 60);
}

static void
gfu_main_signals_cb (GDBusProxy *proxy,
		     const gchar *sender_name,
		     const gchar *signal_name,
		     GVariant *parameters,
		     GfuMain *self)
{
	g_autoptr(FwupdDevice) dev = NULL;
	if (g_strcmp0 (signal_name, "DeviceAdded") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_debug ("Emitting ::device-added(%s)",
			 fwupd_device_get_id (dev));
		gfu_main_device_added_cb (self->client, dev, self);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceRemoved") == 0) {
		dev = fwupd_device_from_variant (parameters);
		g_debug ("Emitting ::device-removed(%s)",
			 fwupd_device_get_id (dev));
		gfu_main_device_removed_cb (self->client, dev, self);
		return;
	}
	if (g_strcmp0 (signal_name, "DeviceChanged") == 0) {
		FwupdStatus status = fwupd_client_get_status (self->client);
		gint percentage;
		g_autoptr(GString) status_str = g_string_new (NULL);
		g_autofree gchar *device_str = NULL;

		dev = fwupd_device_from_variant (parameters);

		/* update progress */
		percentage = fwupd_client_get_percentage (self->client);
		g_string_append_printf (status_str, "%s: %d%%\n",
					gfu_status_to_string (status),
					percentage);

		/* once we have good data show an estimate of time remaining */
		if (gfu_main_estimate_ready (self, percentage)) {
			g_autofree gchar *remaining = gfu_main_time_remaining_str (self);
			if (remaining != NULL)
				g_string_append_printf (status_str, "%s…", remaining);
		}
		gfu_main_set_install_status_label (self, status_str->str);

		/* same as last time, so ignore */
		if (self->device != NULL &&
		    fwupd_device_compare (self->device, dev) == 0)
			return;

		device_str = gfu_operation_to_string (self->current_operation,
						      dev);
		gfu_main_set_install_loading_label (self, device_str);

		return;
	}

	g_debug ("Unknown signal name '%s' from %s", signal_name, sender_name);
}

static void
gfu_main_proxy_new_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	GfuMain *self = (GfuMain *) user_data;
	self->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

	if (self->proxy == NULL) {
		gfu_main_error_dialog (self, _("Error connecting to fwupd"), error->message);
		return;
	}

	/* async call for devices */
	g_dbus_proxy_call (self->proxy,
			   "GetDevices",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   self->cancellable,
			   (GAsyncReadyCallback) gfu_main_update_devices_cb,
			   self);

	/* async call for remotes */
	g_dbus_proxy_call (self->proxy,
			   "GetRemotes",
			   NULL,
			   G_DBUS_CALL_FLAGS_NONE,
			   -1,
			   self->cancellable,
			   (GAsyncReadyCallback) gfu_main_update_remotes_cb,
			   self);

	/* connecting signals for updating devices */
	g_signal_connect (self->proxy, "g-signal",
			  G_CALLBACK (gfu_main_signals_cb), self);
}

static void
gfu_main_device_unlock_cb (GtkWidget *widget, GfuMain *self)
{
	g_autoptr(GError) error = NULL;
	if (!fwupd_client_unlock (self->client,
				  fwupd_device_get_id (self->device),
				  self->cancellable,
				  &error)) {
		gfu_main_error_dialog (self, _("Failed to unlock device"), error->message);
	} else {
		gfu_main_reboot_shutdown_prompt (self);
	}
}

static void
gfu_main_infobar_close_cb (GtkInfoBar *infobar, gpointer user_data)
{
	gtk_info_bar_set_revealed (infobar, FALSE);
}

static void
gfu_main_infobar_response_cb (GtkInfoBar *infobar, gint response_id, gpointer user_data)
{
	if (response_id == GTK_RESPONSE_CLOSE)
		gtk_info_bar_set_revealed (infobar, FALSE);
}

static void
gfu_main_startup_cb (GApplication *application, GfuMain *self)
{
	GtkWidget *w;
	GtkWidget *main_window;
	gint retval;
	g_autofree gchar *filename = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* add application menu items */
	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 self);

	/* get UI */
	self->builder = gtk_builder_new ();
	retval = gtk_builder_add_from_resource (self->builder,
						"/org/gnome/Firmware/gfu-main.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	main_window = GTK_WIDGET (gtk_builder_get_object (self->builder, "dialog_main"));
	gtk_application_add_window (self->application, GTK_WINDOW (main_window));

	/* hide window first so that the dialogue resizes itself without redrawing */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (w), "loading");

	/* buttons */
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_unlock"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_device_unlock_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_verify"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_device_verify_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_verify_update"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_device_verify_update_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_releases"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_device_releases_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_button_back_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_install"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_release_install_file_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_infobar_enable_lvfs"));
	g_signal_connect (w, "clicked",
			  G_CALLBACK (gfu_main_enable_lvfs_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "listbox_main"));
	g_signal_connect (w, "row-selected",
			  G_CALLBACK (gfu_main_device_row_selected_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "listbox_firmware"));
	g_signal_connect (w, "row-selected",
			  G_CALLBACK (gfu_main_release_row_selected_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_enable_lvfs"));
	g_signal_connect (w, "close",
			  G_CALLBACK (gfu_main_infobar_close_cb), self);
	w = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_enable_lvfs"));
	g_signal_connect (w, "response",
			  G_CALLBACK (gfu_main_infobar_response_cb), self);

	/* show main UI */
	gfu_main_update_title (self);
	gfu_main_refresh_ui (self);
	gtk_widget_show (main_window);

	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_NONE,
				  NULL,
				  FWUPD_DBUS_SERVICE,
				  FWUPD_DBUS_PATH,
				  FWUPD_DBUS_INTERFACE,
				  self->cancellable,
				  (GAsyncReadyCallback) gfu_main_proxy_new_cb,
				  self);
}

static void
gfu_main_free (GfuMain *self)
{
	if (self->builder != NULL)
		g_object_unref (self->builder);
	if (self->cancellable != NULL)
		g_object_unref (self->cancellable);
	if (self->device != NULL)
		g_object_unref (self->device);
	if (self->release != NULL)
		g_object_unref (self->release);
	if (self->client != NULL)
		g_object_unref (self->client);
	if (self->application != NULL)
		g_object_unref (self->application);
	if (self->releases != NULL)
		g_ptr_array_unref (self->releases);
	if (self->proxy != NULL)
		g_object_unref (self->proxy);
	if (self->soup_session != NULL)
		g_object_unref (self->soup_session);
	g_timer_destroy (self->time_elapsed);
	g_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GfuMain, gfu_main_free)

int
main (int argc, char **argv)
{
	gboolean verbose = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GfuMain) self = g_new0 (GfuMain, 1);
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
			/* TRANSLATORS: command line option */
			_("Show extra debugging information"), NULL },
		{ NULL}
	};

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* TRANSLATORS: command description */
	context = g_option_context_new (_("GNOME Firmware"));
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		/* TRANSLATORS: the user has sausages for fingers */
		g_print ("%s: %s\n", _("Failed to parse command line options"),
			 error->message);
		return EXIT_FAILURE;
	}

	self->cancellable = g_cancellable_new ();
	self->client = fwupd_client_new ();
	self->time_elapsed = g_timer_new ();

	/* ensure single instance */
	self->application = gtk_application_new ("org.gnome.Firmware", 0);
	g_signal_connect (self->application, "startup",
			  G_CALLBACK (gfu_main_startup_cb), self);
	g_signal_connect (self->application, "activate",
			  G_CALLBACK (gfu_main_activate_cb), self);
	/* set verbose? */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);

	/* wait */
	return g_application_run (G_APPLICATION (self->application), argc, argv);
}
