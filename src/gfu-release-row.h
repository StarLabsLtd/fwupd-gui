/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>
#include <fwupd.h>

G_BEGIN_DECLS

#define GFU_TYPE_RELEASE_ROW (gfu_release_row_get_type ())

G_DECLARE_DERIVABLE_TYPE (GfuReleaseRow, gfu_release_row, GFU, RELEASE_ROW, GtkListBoxRow)

struct _GfuReleaseRowClass
{
	GtkListBoxRowClass	 parent_class;
};

GtkWidget	*gfu_release_row_new			(FwupdRelease	*release);
FwupdRelease	*gfu_release_row_get_release		(GfuReleaseRow	*self);

G_END_DECLS
