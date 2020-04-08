/*
 * Copyright (C) 2019 Andrew Schwenn <aschwenn@verizon.net>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gfu-common.h"

/* formatting helper functions */

gchar *
gfu_common_checksum_format (const gchar *checksum)
{
	const gchar *checksum_type;
	guint len;
	/* guess type based on length */
	if (checksum == NULL)
		return NULL;
	len = strlen (checksum);
	if (len == 32)
		checksum_type = "MD5";
	else if (len == 40)
		checksum_type = "SHA1";
	else if (len == 64)
		checksum_type = "SHA256";
	else if (len == 128)
		checksum_type = "SHA512";
	else
		checksum_type = "SHA1";
	return g_strdup_printf ("%s(%s)",
				checksum_type,
				checksum);
}

const gchar *
gfu_common_seconds_to_string (guint64 seconds)
{
	guint64 minutes, hours;
	if (seconds == 0)
		return NULL;
	if (seconds >= 60) {
		minutes = seconds / 60;
		seconds = seconds % 60;
		if (minutes >= 60) {
			hours = minutes / 60;
			minutes = minutes % 60;
			return g_strdup_printf ("%lu hr, %lu min, %lu sec", hours, minutes, seconds);
		}
		return g_strdup_printf ("%lu min, %lu sec", minutes, seconds);
	}
	return g_strdup_printf ("%lu sec", seconds);
}

gchar *
gfu_common_xml_to_text (const gchar *xml, GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;

	if (xml == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Null input XML");
		return NULL;
	}

	/* parse XML */
	silo = xb_silo_new_from_xml (xml, error);
	if (silo == NULL)
		return NULL;

	n = xb_silo_get_root (silo);
	while (n != NULL) {
		g_autoptr(XbNode) n2 = NULL;

		/* support <p>, <ul>, <ol> and <li>, ignore all else */
		if (g_strcmp0 (xb_node_get_element (n), "p") == 0) {
			g_string_append_printf (str, "%s\n\n", xb_node_get_text (n));
		} else if (g_strcmp0 (xb_node_get_element (n), "ul") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " • %s\n",
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		} else if (g_strcmp0 (xb_node_get_element (n), "ol") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " %u. %s\n",
								i + 1,
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		}

		n2 = xb_node_get_next (n);
		g_set_object (&n, n2);
	}

	/* remove extra newline */
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);

	/* success */
	return g_string_free (g_steal_pointer (&str), FALSE);
}

const gchar *
gfu_common_device_flags_to_strings (guint64 flags)
{
	g_autoptr(GString) str = g_string_new (NULL);
	for (guint j = 0; j < 64; j++) {
		if ((flags & ((guint64) 1 << j)) == 0)
			continue;
		g_string_append_printf (str, "%s\n",
					fwupd_device_flag_to_string ((guint64) 1 << j));
	}
	if (str->len == 0) {
		g_string_append (str, fwupd_device_flag_to_string (0));
	} else {
		g_string_truncate (str, str->len - 1);
	}
	return g_string_free (g_steal_pointer (&str), FALSE);
}

const gchar *
gfu_common_release_flags_to_strings (guint64 flags)
{
	g_autoptr(GString) str = g_string_new (NULL);
	for (guint j = 0; j < 64; j++) {
		if ((flags & ((guint64) 1 << j)) == 0)
			continue;
		g_string_append_printf (str, "%s\n",
					fwupd_device_flag_to_string ((guint64) 1 << j));
	}
	if (str->len == 0) {
		g_string_append (str, fwupd_release_flag_to_string (0));
	} else {
		g_string_truncate (str, str->len - 1);
	}
	return g_string_free (g_steal_pointer (&str), FALSE);
}

/* handle needs-reboot and needs-shutdown */

gboolean
gfu_common_system_shutdown (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;

#ifdef HAVE_LOGIND
	/* shutdown using logind */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.login1",
					   "/org/freedesktop/login1",
					   "org.freedesktop.login1.Manager",
					   "PowerOff",
					   g_variant_new ("(b)", TRUE),
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#elif defined(HAVE_CONSOLEKIT)
	/* shutdown using ConsoleKit */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager",
					   "Stop",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_ARGS,
			     "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

gboolean
gfu_common_system_reboot (GError **error)
{
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GVariant) val = NULL;

	connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
	if (connection == NULL)
		return FALSE;
#ifdef HAVE_LOGIND
	/* reboot using logind */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.login1",
					   "/org/freedesktop/login1",
					   "org.freedesktop.login1.Manager",
					   "Reboot",
					   g_variant_new ("(b)", TRUE),
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#elif defined(HAVE_CONSOLEKIT)
	/* reboot using ConsoleKit */
	val = g_dbus_connection_call_sync (connection,
					   "org.freedesktop.ConsoleKit",
					   "/org/freedesktop/ConsoleKit/Manager",
					   "org.freedesktop.ConsoleKit.Manager",
					   "Restart",
					   NULL,
					   NULL,
					   G_DBUS_CALL_FLAGS_NONE,
					   -1,
					   NULL,
					   error);
#else
	g_set_error_literal (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR,
			     "No supported backend compiled in to perform the operation.");
#endif
	return val != NULL;
}

/* installation helper functions */

gboolean
gfu_common_mkdir_parent (const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (filename);
	g_debug ("creating path %s", parent);
	if (g_mkdir_with_parents (parent, 0755) == -1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to create '%s': %s",
			     parent, g_strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gchar *
gfu_get_user_cache_path (const gchar *fn)
{
	g_autofree gchar *basename = g_path_get_basename (fn);

	return g_build_filename (g_get_user_cache_dir (), "gfu", basename, NULL);
}

gboolean
gfu_common_file_exists_with_checksum (const gchar *fn,
				   const gchar *checksum_expected,
				   GChecksumType checksum_type)
{
	gsize len = 0;
	g_autofree gchar *checksum_actual = NULL;
	g_autofree gchar *data = NULL;

	if (!g_file_get_contents (fn, &data, &len, NULL))
		return FALSE;
	checksum_actual = g_compute_checksum_for_data (checksum_type,
						       (guchar *) data, len);
	return g_strcmp0 (checksum_expected, checksum_actual) == 0;
}

SoupSession *
gfu_common_setup_networking (GError **error)
{
	const gchar *http_proxy;
	g_autofree gchar *user_agent = NULL;
	g_autoptr(SoupSession) session = NULL;

	/* create the soup session */
	user_agent = fwupd_build_user_agent (GETTEXT_PACKAGE, VERSION);
	session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
						 SOUP_SESSION_TIMEOUT, 60,
						 NULL);
	if (session == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to setup networking");
		return NULL;
	}

	/* set the proxy */
	http_proxy = g_getenv ("https_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTPS_PROXY");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("http_proxy");
	if (http_proxy == NULL)
		http_proxy = g_getenv ("HTTP_PROXY");
	if (http_proxy != NULL && strlen (http_proxy) > 0) {
		g_autoptr(SoupURI) proxy_uri = soup_uri_new (http_proxy);
		if (proxy_uri == NULL) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid proxy URI: %s", http_proxy);
			return NULL;
		}
		g_object_set (session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
	}

	/* this disables the double-compression of the firmware.xml.gz file */
	soup_session_remove_feature_by_type (session, SOUP_TYPE_CONTENT_DECODER);
	return g_steal_pointer (&session);
}

/* GTK helper functions */

gchar *
gfu_common_device_flag_to_string (guint64 device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_NONE) {
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL) {
		/* TRANSLATORS: Device cannot be removed easily*/
		return _("Internal device");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE) {
		/* TRANSLATORS: Device is updatable in this or any other mode */
		return _("Updatable");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_OFFLINE) {
		/* TRANSLATORS: Update can only be done from offline mode */
		return _("Update requires a reboot");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC) {
		/* TRANSLATORS: Must be plugged in to an outlet */
		return _("Requires AC power");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED) {
		/* TRANSLATORS: Is locked and can be unlocked */
		return _("Device is locked");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED) {
		/* TRANSLATORS: Is found in current metadata */
		return _("Supported on LVFS");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER) {
		/* TRANSLATORS: Requires a bootloader mode to be manually enabled by the user */
		return _("Requires a bootloader");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_REBOOT) {
		/* TRANSLATORS: Requires a reboot to apply firmware or to reload hardware */
		return _("Needs a reboot after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN) {
		/* TRANSLATORS: Requires system shutdown to apply firmware */
		return _("Needs shutdown after installation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_REPORTED) {
		/* TRANSLATORS: Has been reported to a metadata server */
		return _("Reported to LVFS");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NOTIFIED) {
		/* TRANSLATORS: User has been notified */
		return _("User has been notified");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST) {
		/* TRANSLATORS: Install composite firmware on the parent before the child */
		return _("Install to parent device first");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER) {
		/* TRANSLATORS: Is currently in bootloader mode */
		return _("Is in bootloader mode");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG) {
		/* TRANSLATORS: The hardware is waiting to be replugged */
		return _("Hardware is waiting to be replugged");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_IGNORE_VALIDATION) {
		/* TRANSLATORS: Ignore validation safety checks when flashing this device */
		return _("Ignore validation safety checks");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) {
		/* TRANSLATORS: Device update needs to be separately activated */
		return _("Device update needs activation");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_ENSURE_SEMVER) {
		/* skip */
		return NULL;
	}
#if FWUPD_CHECK_VERSION(1,3,2)
	if (device_flag == FWUPD_DEVICE_FLAG_HISTORICAL) {
		/* skip */
		return NULL;
	}
#endif
#if FWUPD_CHECK_VERSION(1,3,3)
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_SUPPORTED) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_WILL_DISAPPEAR) {
		/* TRANSLATORS: Device will not return after update completes */
		return _("Device will not re-appear after update completes");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY) {
		/* TRANSLATORS: Device supports some form of checksum verification */
		return _("Cryptographic hash verification is available");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE) {
		/* skip */
		return NULL;
	}
	if (device_flag == FWUPD_DEVICE_FLAG_DUAL_IMAGE) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device stages updates");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_SELF_RECOVERY) {
		/* TRANSLATORS: Device supports a safety mechanism for flashing */
		return _("Device can recover flash failures");
	}
	if (device_flag == FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE) {
		/* TRANSLATORS: Device remains usable during update */
		return _("Device is usable for the duration of the update");
	}
#endif
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN) {
		return NULL;
	}
	return NULL;
}

gchar *
gfu_common_device_icon_from_flag (FwupdDeviceFlags device_flag)
{
	if (device_flag == FWUPD_DEVICE_FLAG_INTERNAL)
		return "drive-harddisk-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_UPDATABLE)
		return "software-update-available-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_ONLY_OFFLINE)
		return "network-offline-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_REQUIRE_AC)
		return "battery-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_LOCKED)
		return "locked-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_SUPPORTED)
		return "security-high-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)
		return "computer-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_REBOOT)
		return "system-reboot-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN)
		return "system-shutdown-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_REPORTED)
		return "task-due-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NOTIFIED)
		return "task-due-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION)
		return "system-run-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST)
		return "system-software-install-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_IS_BOOTLOADER)
		return "computer-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)
		return "battery-low-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_IGNORE_VALIDATION)
		return "dialog-error-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED)
		return "media-floppy-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NO_AUTO_INSTANCE_IDS)
		return "dialog-error-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION)
		return "emblem-important-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_ENSURE_SEMVER)
		return "emblem-important-symbolic";
#if FWUPD_CHECK_VERSION(1,3,3)
	if (device_flag == FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)
		return "emblem-important-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_CAN_VERIFY)
		return "emblem-important-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_DUAL_IMAGE)
		return "emblem-important-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_SELF_RECOVERY)
		return "emblem-important-symbolic";
	if (device_flag == FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)
		return "emblem-important-symbolic";

#endif
	if (device_flag == FWUPD_DEVICE_FLAG_UNKNOWN)
		return "unknown-symbolic";
	return NULL;
}

gchar *
gfu_operation_to_string (GfuOperation operation, FwupdDevice *dev)
{
	/* show message in progressbar */
	if (operation == GFU_OPERATION_UPDATE) {
		/* TRANSLATORS: %1 is a device name */
		return g_strdup_printf (_("Updating %s…"),
					fwupd_device_get_name (dev));
	} else if (operation == GFU_OPERATION_DOWNGRADE) {
		/* TRANSLATORS: %1 is a device name */
		return g_strdup_printf (_("Downgrading %s…"),
					fwupd_device_get_name (dev));
	} else if (operation == GFU_OPERATION_INSTALL) {
		/* TRANSLATORS: %1 is a device name  */
		return g_strdup_printf (_("Installing on %s…"),
					fwupd_device_get_name (dev));
	}
	g_warning ("no GfuOperation set");
	return NULL;
}

const gchar *
gfu_status_to_string (FwupdStatus status)
{
	switch (status) {
	case FWUPD_STATUS_IDLE:
		/* TRANSLATORS: daemon is inactive */
		return _("Idle…");
		break;
	case FWUPD_STATUS_DECOMPRESSING:
		/* TRANSLATORS: decompressing the firmware file */
		return _("Decompressing…");
		break;
	case FWUPD_STATUS_LOADING:
		/* TRANSLATORS: parsing the firmware information */
		return _("Loading…");
		break;
	case FWUPD_STATUS_DEVICE_RESTART:
		/* TRANSLATORS: restarting the device to pick up new F/W */
		return _("Restarting device…");
		break;
	case FWUPD_STATUS_DEVICE_READ:
		/* TRANSLATORS: reading from the flash chips */
		return _("Reading…");
		break;
	case FWUPD_STATUS_DEVICE_WRITE:
		/* TRANSLATORS: writing to the flash chips */
		return _("Writing…");
		break;
	case FWUPD_STATUS_DEVICE_ERASE:
		/* TRANSLATORS: erasing contents of the flash chips */
		return _("Erasing…");
		break;
	case FWUPD_STATUS_DEVICE_VERIFY:
		/* TRANSLATORS: verifying we wrote the firmware correctly */
		return _("Verifying…");
		break;
	case FWUPD_STATUS_SCHEDULING:
		/* TRANSLATORS: scheduing an update to be done on the next boot */
		return _("Scheduling…");
		break;
	case FWUPD_STATUS_DOWNLOADING:
		/* TRANSLATORS: downloading from a remote server */
		return _("Downloading…");
		break;
	case FWUPD_STATUS_WAITING_FOR_AUTH:
		/* TRANSLATORS: waiting for user to authenticate */
		return _("Authenticating…");
		break;
	case FWUPD_STATUS_DEVICE_BUSY:
		/* TRANSLATORS: waiting for device to do something */
		return _("Waiting…");
		break;
	default:
		break;
	}

	/* TRANSLATORS: currect daemon status is unknown */
	return _("Unknown");
}
