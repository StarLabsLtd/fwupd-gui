/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>
#include <fwupd.h>

G_BEGIN_DECLS

#define GFU_TYPE_DEVICE_ROW (gfu_device_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GfuDeviceRow, gfu_device_row, GFU, DEVICE_ROW, GtkListBoxRow)

struct _GfuDeviceRowClass
{
	GtkListBoxRowClass	 parent_class;
};

GtkWidget	*gfu_device_row_new			(FwupdDevice	*device);
FwupdDevice	*gfu_device_row_get_device		(GfuDeviceRow	*self);

G_END_DECLS
