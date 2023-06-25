/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2009 Carlos Garnacho <carlosg@gnome.org>
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

#include "config.h"

#include "gdkx11device-xi2.h"
#include "gdkdeviceprivate.h"

#include "gdkintl.h"
#include "gdkasync.h"
#include "gdkprivate-x11.h"

#ifdef XINPUT_2

#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>

#endif

struct _GdkX11DeviceXI2
{
  GdkDevice parent_instance;

  gint device_id;
};

struct _GdkX11DeviceXI2Class
{
  GdkDeviceClass parent_class;
};

G_DEFINE_TYPE (GdkX11DeviceXI2, gdk_x11_device_xi2, GDK_TYPE_DEVICE)

#ifdef XINPUT_2

static void gdk_x11_device_xi2_get_property (GObject      *object,
                                             guint         prop_id,
                                             GValue       *value,
                                             GParamSpec   *pspec);
static void gdk_x11_device_xi2_set_property (GObject      *object,
                                             guint         prop_id,
                                             const GValue *value,
                                             GParamSpec   *pspec);

static void gdk_x11_device_xi2_get_state (GdkDevice       *device,
                                          GdkWindow       *window,
                                          gdouble         *axes,
                                          GdkModifierType *mask);
static void gdk_x11_device_xi2_set_window_cursor (GdkDevice *device,
                                                  GdkWindow *window,
                                                  GdkCursor *cursor);
static void gdk_x11_device_xi2_warp (GdkDevice *device,
                                     GdkScreen *screen,
                                     gint       x,
                                     gint       y);
static gboolean gdk_x11_device_xi2_query_state (GdkDevice        *device,
                                                GdkWindow        *window,
                                                GdkWindow       **root_window,
                                                GdkWindow       **child_window,
                                                gint             *root_x,
                                                gint             *root_y,
                                                gint             *win_x,
                                                gint             *win_y,
                                                GdkModifierType  *mask);

static GdkGrabStatus gdk_x11_device_xi2_grab   (GdkDevice     *device,
                                                GdkWindow     *window,
                                                gboolean       owner_events,
                                                GdkEventMask   event_mask,
                                                GdkWindow     *confine_to,
                                                GdkCursor     *cursor,
                                                guint32        time_);
static void          gdk_x11_device_xi2_ungrab (GdkDevice     *device,
                                                guint32        time_);

static GdkWindow * gdk_x11_device_xi2_window_at_position (GdkDevice       *device,
                                                          gint            *win_x,
                                                          gint            *win_y,
                                                          GdkModifierType *mask,
                                                          gboolean         get_toplevel);
static void  gdk_x11_device_xi2_select_window_events (GdkDevice    *device,
                                                      GdkWindow    *window,
                                                      GdkEventMask  event_mask);


enum {
  PROP_0,
  PROP_DEVICE_ID
};

static void
gdk_x11_device_xi2_class_init (GdkX11DeviceXI2Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  object_class->get_property = gdk_x11_device_xi2_get_property;
  object_class->set_property = gdk_x11_device_xi2_set_property;

  device_class->get_state = gdk_x11_device_xi2_get_state;
  device_class->set_window_cursor = gdk_x11_device_xi2_set_window_cursor;
  device_class->warp = gdk_x11_device_xi2_warp;
  device_class->query_state = gdk_x11_device_xi2_query_state;
  device_class->grab = gdk_x11_device_xi2_grab;
  device_class->ungrab = gdk_x11_device_xi2_ungrab;
  device_class->window_at_position = gdk_x11_device_xi2_window_at_position;
  device_class->select_window_events = gdk_x11_device_xi2_select_window_events;

  g_object_class_install_property (object_class,
                                   PROP_DEVICE_ID,
                                   g_param_spec_int ("device-id",
                                                     P_("Device ID"),
                                                     P_("Device identifier"),
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gdk_x11_device_xi2_init (GdkX11DeviceXI2 *device)
{
}

static void
gdk_x11_device_xi2_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      g_value_set_int (value, device_xi2->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_x11_device_xi2_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      device_xi2->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_x11_device_xi2_get_state (GdkDevice       *device,
                              GdkWindow       *window,
                              gdouble         *axes,
                              GdkModifierType *mask)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);

  if (axes)
    {
      GdkDisplay *display;
      XIDeviceInfo *info;
      gint i, j, ndevices;

      display = gdk_device_get_display (device);

      gdk_x11_display_error_trap_push (display);
      info = XIQueryDevice (GDK_DISPLAY_XDISPLAY (display),
                            device_xi2->device_id, &ndevices);
      gdk_x11_display_error_trap_pop_ignored (display);

      for (i = 0, j = 0; info && i < info->num_classes; i++)
        {
          XIAnyClassInfo *class_info = info->classes[i];
          GdkAxisUse use;
          gdouble value;

          if (class_info->type != XIValuatorClass)
            continue;

          value = ((XIValuatorClassInfo *) class_info)->value;
          use = gdk_device_get_axis_use (device, j);

          switch (use)
            {
            case GDK_AXIS_X:
            case GDK_AXIS_Y:
            case GDK_AXIS_IGNORE:
              if (gdk_device_get_mode (device) == GDK_MODE_WINDOW)
                _gdk_device_translate_window_coord (device, window, j, value, &axes[j]);
              else
                {
                  gint root_x, root_y;

                  /* FIXME: Maybe root coords chaching should happen here */
                  gdk_window_get_origin (window, &root_x, &root_y);
                  _gdk_device_translate_screen_coord (device, window,
                                                      root_x, root_y,
                                                      j, value,
                                                      &axes[j]);
                }
              break;
            default:
              _gdk_device_translate_axis (device, j, value, &axes[j]);
              break;
            }

          j++;
        }

      if (info)
        XIFreeDeviceInfo (info);
    }

  if (mask)
    gdk_x11_device_xi2_query_state (device, window,
                                    NULL, NULL,
                                    NULL, NULL,
                                    NULL, NULL,
                                    mask);
}

static void
gdk_x11_device_xi2_set_window_cursor (GdkDevice *device,
                                      GdkWindow *window,
                                      GdkCursor *cursor)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);

  /* Non-master devices don't have a cursor */
  if (gdk_device_get_device_type (device) != GDK_DEVICE_TYPE_MASTER)
    return;

  if (cursor)
    XIDefineCursor (GDK_WINDOW_XDISPLAY (window),
                    device_xi2->device_id,
                    GDK_WINDOW_XID (window),
                    gdk_x11_cursor_get_xcursor (cursor));
  else
    XIUndefineCursor (GDK_WINDOW_XDISPLAY (window),
                      device_xi2->device_id,
                      GDK_WINDOW_XID (window));
}

static void
gdk_x11_device_xi2_warp (GdkDevice *device,
                         GdkScreen *screen,
                         gint       x,
                         gint       y)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  Window dest;

  dest = GDK_WINDOW_XID (gdk_screen_get_root_window (screen));

  XIWarpPointer (GDK_SCREEN_XDISPLAY (screen),
                 device_xi2->device_id,
                 None, dest,
                 0, 0, 0, 0, x, y);
}

static gboolean
gdk_x11_device_xi2_query_state (GdkDevice        *device,
                                GdkWindow        *window,
                                GdkWindow       **root_window,
                                GdkWindow       **child_window,
                                gint             *root_x,
                                gint             *root_y,
                                gint             *win_x,
                                gint             *win_y,
                                GdkModifierType  *mask)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  GdkDisplay *display;
  GdkScreen *default_screen;
  Window xroot_window, xchild_window;
  gdouble xroot_x, xroot_y, xwin_x, xwin_y;
  XIButtonState button_state;
  XIModifierState mod_state;
  XIGroupState group_state;

  if (!window || GDK_WINDOW_DESTROYED (window))
    return FALSE;

  display = gdk_window_get_display (window);
  default_screen = gdk_display_get_default_screen (display);

  if (G_LIKELY (GDK_X11_DISPLAY (display)->trusted_client))
    {
      if (!XIQueryPointer (GDK_WINDOW_XDISPLAY (window),
                           device_xi2->device_id,
                           GDK_WINDOW_XID (window),
                           &xroot_window,
                           &xchild_window,
                           &xroot_x, &xroot_y,
                           &xwin_x, &xwin_y,
                           &button_state,
                           &mod_state,
                           &group_state))
        return FALSE;
    }
  else
    {
      XSetWindowAttributes attributes;
      Display *xdisplay;
      Window xwindow, w;

      /* FIXME: untrusted clients not multidevice-safe */
      xdisplay = GDK_SCREEN_XDISPLAY (default_screen);
      xwindow = GDK_SCREEN_XROOTWIN (default_screen);

      w = XCreateWindow (xdisplay, xwindow, 0, 0, 1, 1, 0,
                         CopyFromParent, InputOnly, CopyFromParent,
                         0, &attributes);
      XIQueryPointer (xdisplay, device_xi2->device_id,
                      w,
                      &xroot_window,
                      &xchild_window,
                      &xroot_x, &xroot_y,
                      &xwin_x, &xwin_y,
                      &button_state,
                      &mod_state,
                      &group_state);
      XDestroyWindow (xdisplay, w);
    }

  if (root_window)
    *root_window = gdk_x11_window_lookup_for_display (display, xroot_window);

  if (child_window)
    *child_window = gdk_x11_window_lookup_for_display (display, xchild_window);

  if (root_x)
    *root_x = (gint) xroot_x;

  if (root_y)
    *root_y = (gint) xroot_y;

  if (win_x)
    *win_x = (gint) xwin_x;

  if (win_y)
    *win_y = (gint) xwin_y;

  if (mask)
    *mask = _gdk_x11_device_xi2_translate_state (&mod_state, &button_state, &group_state);

  free (button_state.mask);

  return TRUE;
}

static GdkGrabStatus
gdk_x11_device_xi2_grab (GdkDevice    *device,
                         GdkWindow    *window,
                         gboolean      owner_events,
                         GdkEventMask  event_mask,
                         GdkWindow    *confine_to,
                         GdkCursor    *cursor,
                         guint32       time_)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  GdkDisplay *display;
  XIEventMask mask;
  Window xwindow;
  Cursor xcursor;
  gint status;

  display = gdk_device_get_display (device);

  /* FIXME: confine_to is actually unused */

  xwindow = GDK_WINDOW_XID (window);

  if (!cursor)
    xcursor = None;
  else
    {
      _gdk_x11_cursor_update_theme (cursor);
      xcursor = gdk_x11_cursor_get_xcursor (cursor);
    }

  mask.deviceid = device_xi2->device_id;
  mask.mask = _gdk_x11_device_xi2_translate_event_mask (event_mask, &mask.mask_len);

#ifdef G_ENABLE_DEBUG
  if (_gdk_debug_flags & GDK_DEBUG_NOGRABS)
    status = GrabSuccess;
  else
#endif
  status = XIGrabDevice (GDK_DISPLAY_XDISPLAY (display),
                         device_xi2->device_id,
                         xwindow,
                         time_,
                         xcursor,
                         GrabModeAsync, GrabModeAsync,
                         owner_events,
                         &mask);

  g_free (mask.mask);

  _gdk_x11_display_update_grab_info (display, device, status);

  return _gdk_x11_convert_grab_status (status);
}

static void
gdk_x11_device_xi2_ungrab (GdkDevice *device,
                           guint32    time_)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  GdkDisplay *display;
  gulong serial;

  display = gdk_device_get_display (device);
  serial = NextRequest (GDK_DISPLAY_XDISPLAY (display));

  XIUngrabDevice (GDK_DISPLAY_XDISPLAY (display), device_xi2->device_id, time_);

  _gdk_x11_display_update_grab_info_ungrab (display, device, time_, serial);
}

static GdkWindow *
gdk_x11_device_xi2_window_at_position (GdkDevice       *device,
                                       gint            *win_x,
                                       gint            *win_y,
                                       GdkModifierType *mask,
                                       gboolean         get_toplevel)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  GdkDisplay *display;
  GdkScreen *screen;
  Display *xdisplay;
  GdkWindow *window;
  Window xwindow, root, child, last = None;
  gdouble xroot_x, xroot_y, xwin_x, xwin_y;
  XIButtonState button_state = { 0 };
  XIModifierState mod_state;
  XIGroupState group_state;

  display = gdk_device_get_display (device);
  screen = gdk_display_get_default_screen (display);

  /* This function really only works if the mouse pointer is held still
   * during its operation. If it moves from one leaf window to another
   * than we'll end up with inaccurate values for win_x, win_y
   * and the result.
   */
  gdk_x11_display_grab (display);

  xdisplay = GDK_SCREEN_XDISPLAY (screen);
  xwindow = GDK_SCREEN_XROOTWIN (screen);

  if (G_LIKELY (GDK_X11_DISPLAY (display)->trusted_client))
    {
      XIQueryPointer (xdisplay,
                      device_xi2->device_id,
                      xwindow,
                      &root, &child,
                      &xroot_x, &xroot_y,
                      &xwin_x, &xwin_y,
                      &button_state,
                      &mod_state,
                      &group_state);

      if (root == xwindow)
        xwindow = child;
      else
        xwindow = root;
    }
  else
    {
      gint i, screens, width, height;
      GList *toplevels, *list;
      Window pointer_window, root, child;

      /* FIXME: untrusted clients case not multidevice-safe */
      pointer_window = None;
      screens = gdk_display_get_n_screens (display);

      for (i = 0; i < screens; ++i)
        {
          screen = gdk_display_get_screen (display, i);
          toplevels = gdk_screen_get_toplevel_windows (screen);
          for (list = toplevels; list != NULL; list = g_list_next (list))
            {
              window = GDK_WINDOW (list->data);
              xwindow = GDK_WINDOW_XID (window);

              /* Free previous button mask, if any */
              g_free (button_state.mask);

              gdk_x11_display_error_trap_push (display);
              XIQueryPointer (xdisplay,
                              device_xi2->device_id,
                              xwindow,
                              &root, &child,
                              &xroot_x, &xroot_y,
                              &xwin_x, &xwin_y,
                              &button_state,
                              &mod_state,
                              &group_state);
              if (gdk_x11_display_error_trap_pop (display))
                continue;
              if (child != None)
                {
                  pointer_window = child;
                  break;
                }
              gdk_window_get_geometry (window, NULL, NULL, &width, &height);
              if (xwin_x >= 0 && xwin_y >= 0 && xwin_x < width && xwin_y < height)
                {
                  /* A childless toplevel, or below another window? */
                  XSetWindowAttributes attributes;
                  Window w;

                  free (button_state.mask);

                  w = XCreateWindow (xdisplay, xwindow, (int)xwin_x, (int)xwin_y, 1, 1, 0,
                                     CopyFromParent, InputOnly, CopyFromParent,
                                     0, &attributes);
                  XMapWindow (xdisplay, w);
                  XIQueryPointer (xdisplay,
                                  device_xi2->device_id,
                                  xwindow,
                                  &root, &child,
                                  &xroot_x, &xroot_y,
                                  &xwin_x, &xwin_y,
                                  &button_state,
                                  &mod_state,
                                  &group_state);
                  XDestroyWindow (xdisplay, w);
                  if (child == w)
                    {
                      pointer_window = xwindow;
                      break;
                    }
                }
            }

          g_list_free (toplevels);
          if (pointer_window != None)
            break;
        }

      xwindow = pointer_window;
    }

  while (xwindow)
    {
      last = xwindow;
      free (button_state.mask);

      gdk_x11_display_error_trap_push (display);
      XIQueryPointer (xdisplay,
                      device_xi2->device_id,
                      xwindow,
                      &root, &xwindow,
                      &xroot_x, &xroot_y,
                      &xwin_x, &xwin_y,
                      &button_state,
                      &mod_state,
                      &group_state);
      if (gdk_x11_display_error_trap_pop (display))
        break;

      if (get_toplevel && last != root &&
          (window = gdk_x11_window_lookup_for_display (display, last)) != NULL &&
          GDK_WINDOW_TYPE (window) != GDK_WINDOW_FOREIGN)
        {
          xwindow = last;
          break;
        }
    }

  gdk_x11_display_ungrab (display);

  window = gdk_x11_window_lookup_for_display (display, last);

  if (win_x)
    *win_x = (window) ? (gint) xwin_x : -1;

  if (win_y)
    *win_y = (window) ? (gint) xwin_y : -1;

  if (mask)
    *mask = _gdk_x11_device_xi2_translate_state (&mod_state, &button_state, &group_state);

  free (button_state.mask);

  return window;
}

static void
gdk_x11_device_xi2_select_window_events (GdkDevice    *device,
                                         GdkWindow    *window,
                                         GdkEventMask  event_mask)
{
  GdkX11DeviceXI2 *device_xi2 = GDK_X11_DEVICE_XI2 (device);
  XIEventMask evmask;

  evmask.deviceid = device_xi2->device_id;
  evmask.mask = _gdk_x11_device_xi2_translate_event_mask (event_mask, &evmask.mask_len);

  XISelectEvents (GDK_WINDOW_XDISPLAY (window),
                  GDK_WINDOW_XID (window),
                  &evmask, 1);

  g_free (evmask.mask);
}

guchar *
_gdk_x11_device_xi2_translate_event_mask (GdkEventMask  event_mask,
                                          gint         *len)
{
  guchar *mask;

  *len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (guchar, *len);

  if (event_mask & GDK_POINTER_MOTION_MASK ||
      event_mask & GDK_POINTER_MOTION_HINT_MASK)
    XISetMask (mask, XI_Motion);

  if (event_mask & GDK_BUTTON_MOTION_MASK ||
      event_mask & GDK_BUTTON1_MOTION_MASK ||
      event_mask & GDK_BUTTON2_MOTION_MASK ||
      event_mask & GDK_BUTTON3_MOTION_MASK)
    {
      XISetMask (mask, XI_ButtonPress);
      XISetMask (mask, XI_ButtonRelease);
      XISetMask (mask, XI_Motion);
    }

  if (event_mask & GDK_SCROLL_MASK)
    {
      XISetMask (mask, XI_ButtonPress);
      XISetMask (mask, XI_ButtonRelease);
    }

  if (event_mask & GDK_BUTTON_PRESS_MASK)
    XISetMask (mask, XI_ButtonPress);

  if (event_mask & GDK_BUTTON_RELEASE_MASK)
    XISetMask (mask, XI_ButtonRelease);

  if (event_mask & GDK_KEY_PRESS_MASK)
    XISetMask (mask, XI_KeyPress);

  if (event_mask & GDK_KEY_RELEASE_MASK)
    XISetMask (mask, XI_KeyRelease);

  if (event_mask & GDK_ENTER_NOTIFY_MASK)
    XISetMask (mask, XI_Enter);

  if (event_mask & GDK_LEAVE_NOTIFY_MASK)
    XISetMask (mask, XI_Leave);

  if (event_mask & GDK_FOCUS_CHANGE_MASK)
    {
      XISetMask (mask, XI_FocusIn);
      XISetMask (mask, XI_FocusOut);
    }

  return mask;
}

guint
_gdk_x11_device_xi2_translate_state (XIModifierState *mods_state,
                                     XIButtonState   *buttons_state,
                                     XIGroupState    *group_state)
{
  guint state = 0;

  if (mods_state)
    state = mods_state->effective;

  if (buttons_state)
    {
      gint len, i;

      /* We're only interested in the first 5 buttons */
      len = MIN (5, buttons_state->mask_len * 8);

      for (i = 0; i < len; i++)
        {
          if (!XIMaskIsSet (buttons_state->mask, i))
            continue;

          switch (i)
            {
            case 1:
              state |= GDK_BUTTON1_MASK;
              break;
            case 2:
              state |= GDK_BUTTON2_MASK;
              break;
            case 3:
              state |= GDK_BUTTON3_MASK;
              break;
            case 4:
              state |= GDK_BUTTON4_MASK;
              break;
            case 5:
              state |= GDK_BUTTON5_MASK;
              break;
            default:
              break;
            }
        }
    }

  if (group_state)
    state |= (group_state->effective) << 13;

  return state;
}

gint
_gdk_x11_device_xi2_get_id (GdkX11DeviceXI2 *device)
{
  g_return_val_if_fail (GDK_IS_X11_DEVICE_XI2 (device), 0);

  return device->device_id;
}

#else /* XINPUT_2 */

static void
gdk_x11_device_xi2_class_init (GdkX11DeviceXI2Class *klass)
{
}

static void
gdk_x11_device_xi2_init (GdkX11DeviceXI2 *device)
{
}

#endif /* XINPUT_2 */
