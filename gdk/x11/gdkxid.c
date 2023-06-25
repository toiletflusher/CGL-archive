/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#include "config.h"

#include "gdkprivate-x11.h"
#include "gdkdisplay-x11.h"

#include <stdio.h>

static guint
gdk_xid_hash (XID *xid)
{
  return *xid;
}

static gboolean
gdk_xid_equal (XID *a, XID *b)
{
  return (*a == *b);
}

void
_gdk_x11_display_add_window (GdkDisplay *display,
                             XID        *xid,
                             GdkWindow  *data)
{
  GdkX11Display *display_x11;

  g_return_if_fail (xid != NULL);
  g_return_if_fail (GDK_IS_DISPLAY (display));

  display_x11 = GDK_X11_DISPLAY (display);

  if (!display_x11->xid_ht)
    display_x11->xid_ht = g_hash_table_new ((GHashFunc) gdk_xid_hash,
                                            (GEqualFunc) gdk_xid_equal);

  if (g_hash_table_lookup (display_x11->xid_ht, xid))
    g_warning ("XID collision, trouble ahead");

  g_hash_table_insert (display_x11->xid_ht, xid, data);
}

void
_gdk_x11_display_remove_window (GdkDisplay *display,
                                XID         xid)
{
  GdkX11Display *display_x11;

  g_return_if_fail (GDK_IS_DISPLAY (display));

  display_x11 = GDK_X11_DISPLAY (display);

  if (display_x11->xid_ht)
    g_hash_table_remove (display_x11->xid_ht, &xid);
}

/**
 * gdk_x11_window_lookup_for_display:
 * @display: (type GdkX11Window): the #GdkDisplay corresponding to the
 *           window handle
 * @window: an XLib <type>Window</type>
 *
 * Looks up the #GdkWindow that wraps the given native window handle.
 *
 * Return value: (transfer none): the #GdkWindow wrapper for the native
 *    window, or %NULL if there is none.
 *
 * Since: 2.24
 */
GdkWindow *
gdk_x11_window_lookup_for_display (GdkDisplay *display,
                                   Window      window)
{
  GdkX11Display *display_x11;
  GdkWindow *data = NULL;

  g_return_val_if_fail (GDK_IS_DISPLAY (display), NULL);

  display_x11 = GDK_X11_DISPLAY (display);

  if (display_x11->xid_ht)
    data = g_hash_table_lookup (display_x11->xid_ht, &window);

  return data;
}
