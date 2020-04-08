/*
 * Copyright (C) 2019 Andrew Schwenn <aschwenn@verizon.net>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <xmlb.h>
#include <libsoup/soup.h>
#include <errno.h>

G_BEGIN_DECLS

typedef enum {
	GFU_OPERATION_UNKNOWN,
	GFU_OPERATION_UPDATE,
	GFU_OPERATION_DOWNGRADE,
	GFU_OPERATION_INSTALL,
	GFU_OPERATION_LAST
} GfuOperation;

/* formatting helper functions */
gchar           *gfu_common_checksum_format             (const gchar	*checksum);
const gchar     *gfu_common_seconds_to_string           (guint64	seconds);
gchar           *gfu_common_xml_to_text                 (const gchar	*xml,
							 GError		**error);
const gchar     *gfu_common_device_flags_to_strings     (guint64	flags);
const gchar     *gfu_common_release_flags_to_strings    (guint64	flags);
const gchar     *gfu_status_to_string                   (FwupdStatus	 status);
gchar           *gfu_operation_to_string                (GfuOperation	 operation,
                                                        FwupdDevice	*device);

/* handle needs-reboot and needs-shutdown */
gboolean        gfu_common_system_shutdown              (GError		**error);
gboolean        gfu_common_system_reboot                (GError		**error);

/* installation helper functions */
gboolean        gfu_common_mkdir_parent                 (const gchar	*filename,
							 GError		**error);
gboolean        gfu_common_file_exists_with_checksum    (const gchar	*fn,
				                         const gchar	*checksum_expected,
				                         GChecksumType	checksum_type);
SoupSession     *gfu_common_setup_networking            (GError		**error);
gchar 		*gfu_get_user_cache_path		(const gchar *fn);

/* GTK helper functions */
gchar		*gfu_common_device_flag_to_string		(guint64	device_flag);
gchar		*gfu_common_device_icon_from_flag		(FwupdDeviceFlags device_flag);

G_END_DECLS
