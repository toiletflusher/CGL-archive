 /*
 * gdkscreen-x11.c
 *
 * Copyright 2001 Sun Microsystems Inc.
 *
 * Erwann Chenede <erwann.chenede@sun.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "gdkscreen-x11.h"
#include "gdkdisplay-x11.h"
#include "gdkprivate-x11.h"

#include <glib.h>

#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>

#ifdef HAVE_SOLARIS_XINERAMA
#include <X11/extensions/xinerama.h>
#endif
#ifdef HAVE_XFREE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#ifdef HAVE_XFIXES
#include <X11/extensions/Xfixes.h>
#endif

#include "gdksettings.c"

static void         gdk_x11_screen_dispose     (GObject		  *object);
static void         gdk_x11_screen_finalize    (GObject		  *object);
static void	    init_randr_support	       (GdkScreen	  *screen);
static void	    deinit_multihead           (GdkScreen         *screen);

enum
{
  WINDOW_MANAGER_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdkX11Screen, gdk_x11_screen, GDK_TYPE_SCREEN)

typedef struct _NetWmSupportedAtoms NetWmSupportedAtoms;

struct _NetWmSupportedAtoms
{
  Atom *atoms;
  gulong n_atoms;
};

struct _GdkX11Monitor
{
  GdkRectangle  geometry;
  XID		output;
  int		width_mm;
  int		height_mm;
  char *	output_name;
  char *	manufacturer;
};


static void
gdk_x11_screen_init (GdkX11Screen *screen)
{
}

static GdkDisplay *
gdk_x11_screen_get_display (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  return GDK_X11_SCREEN (screen)->display;
}

static gint
gdk_x11_screen_get_width (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

  return WidthOfScreen (GDK_X11_SCREEN (screen)->xscreen);
}

static gint
gdk_x11_screen_get_height (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

  return HeightOfScreen (GDK_X11_SCREEN (screen)->xscreen);
}

static gint
gdk_x11_screen_get_width_mm (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);  

  return WidthMMOfScreen (GDK_X11_SCREEN (screen)->xscreen);
}

static gint
gdk_x11_screen_get_height_mm (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

  return HeightMMOfScreen (GDK_X11_SCREEN (screen)->xscreen);
}

static gint
gdk_x11_screen_get_number (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);
  
  return GDK_X11_SCREEN (screen)->screen_num;
}

static GdkWindow *
gdk_x11_screen_get_root_window (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  return GDK_X11_SCREEN (screen)->root_window;
}

static void
_gdk_x11_screen_events_uninit (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  if (x11_screen->xsettings_client)
    {
      xsettings_client_destroy (x11_screen->xsettings_client);
      x11_screen->xsettings_client = NULL;
    }
}

static void
gdk_x11_screen_dispose (GObject *object)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (object);
  int i;

  for (i = 0; i < 32; ++i)
    {
      if (x11_screen->subwindow_gcs[i])
        {
          XFreeGC (x11_screen->xdisplay, x11_screen->subwindow_gcs[i]);
          x11_screen->subwindow_gcs[i] = 0;
        }
    }

  _gdk_x11_screen_events_uninit (GDK_SCREEN (object));

  if (x11_screen->root_window)
    _gdk_window_destroy (x11_screen->root_window, TRUE);

  G_OBJECT_CLASS (gdk_x11_screen_parent_class)->dispose (object);

  x11_screen->xdisplay = NULL;
  x11_screen->xscreen = NULL;
  x11_screen->screen_num = -1;
  x11_screen->xroot_window = None;
  x11_screen->wmspec_check_window = None;
}

static void
gdk_x11_screen_finalize (GObject *object)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (object);
  gint          i;

  if (x11_screen->root_window)
    g_object_unref (x11_screen->root_window);

  /* Visual Part */
  for (i = 0; i < x11_screen->nvisuals; i++)
    g_object_unref (x11_screen->visuals[i]);
  g_free (x11_screen->visuals);
  g_hash_table_destroy (x11_screen->visual_hash);

  g_free (x11_screen->window_manager_name);

  deinit_multihead (GDK_SCREEN (object));
  
  G_OBJECT_CLASS (gdk_x11_screen_parent_class)->finalize (object);
}

static gint
gdk_x11_screen_get_n_monitors (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

  return GDK_X11_SCREEN (screen)->n_monitors;
}

static gint
gdk_x11_screen_get_primary_monitor (GdkScreen *screen)
{
  g_return_val_if_fail (GDK_IS_SCREEN (screen), 0);

  return GDK_X11_SCREEN (screen)->primary_monitor;
}

static gint
gdk_x11_screen_get_monitor_width_mm	(GdkScreen *screen,
					 gint       monitor_num)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  g_return_val_if_fail (GDK_IS_SCREEN (screen), -1);
  g_return_val_if_fail (monitor_num >= 0, -1);
  g_return_val_if_fail (monitor_num < x11_screen->n_monitors, -1);

  return x11_screen->monitors[monitor_num].width_mm;
}

static gint
gdk_x11_screen_get_monitor_height_mm (GdkScreen *screen,
				      gint       monitor_num)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  g_return_val_if_fail (GDK_IS_SCREEN (screen), -1);
  g_return_val_if_fail (monitor_num >= 0, -1);
  g_return_val_if_fail (monitor_num < x11_screen->n_monitors, -1);

  return x11_screen->monitors[monitor_num].height_mm;
}

static gchar *
gdk_x11_screen_get_monitor_plug_name (GdkScreen *screen,
				      gint       monitor_num)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);
  g_return_val_if_fail (monitor_num >= 0, NULL);
  g_return_val_if_fail (monitor_num < x11_screen->n_monitors, NULL);

  return g_strdup (x11_screen->monitors[monitor_num].output_name);
}

/**
 * gdk_x11_screen_get_monitor_output:
 * @screen: (type GdkX11Screen): a #GdkScreen
 * @monitor_num: number of the monitor, between 0 and gdk_screen_get_n_monitors (screen)
 *
 * Gets the XID of the specified output/monitor.
 * If the X server does not support version 1.2 of the RANDR
 * extension, 0 is returned.
 *
 * Returns: the XID of the monitor
 *
 * Since: 2.14
 */
XID
gdk_x11_screen_get_monitor_output (GdkScreen *screen,
                                   gint       monitor_num)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  g_return_val_if_fail (GDK_IS_SCREEN (screen), None);
  g_return_val_if_fail (monitor_num >= 0, None);
  g_return_val_if_fail (monitor_num < x11_screen->n_monitors, None);

  return x11_screen->monitors[monitor_num].output;
}

static void
gdk_x11_screen_get_monitor_geometry (GdkScreen    *screen,
				     gint          monitor_num,
				     GdkRectangle *dest)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  g_return_if_fail (GDK_IS_SCREEN (screen));
  g_return_if_fail (monitor_num >= 0);
  g_return_if_fail (monitor_num < x11_screen->n_monitors);

  if (dest)
    *dest = x11_screen->monitors[monitor_num].geometry;
}

static GdkVisual *
gdk_x11_screen_get_rgba_visual (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  x11_screen = GDK_X11_SCREEN (screen);

  return x11_screen->rgba_visual;
}

/**
 * gdk_x11_screen_get_xscreen:
 * @screen: (type GdkX11Screen): a #GdkScreen.
 * @returns: (transfer none): an Xlib <type>Screen*</type>
 *
 * Returns the screen of a #GdkScreen.
 *
 * Since: 2.2
 */
Screen *
gdk_x11_screen_get_xscreen (GdkScreen *screen)
{
  return GDK_X11_SCREEN (screen)->xscreen;
}

/**
 * gdk_x11_screen_get_screen_number:
 * @screen: (type GdkX11Screen): a #GdkScreen.
 * @returns: the position of @screen among the screens of
 *   its display.
 *
 * Returns the index of a #GdkScreen.
 *
 * Since: 2.2
 */
int
gdk_x11_screen_get_screen_number (GdkScreen *screen)
{
  return GDK_X11_SCREEN (screen)->screen_num;
}

static gboolean
check_is_composited (GdkDisplay *display,
		     GdkX11Screen *x11_screen)
{
  Atom xselection = gdk_x11_atom_to_xatom_for_display (display, x11_screen->cm_selection_atom);
  Window xwindow;
  
  xwindow = XGetSelectionOwner (GDK_DISPLAY_XDISPLAY (display), xselection);

  return xwindow != None;
}

static GdkAtom
make_cm_atom (int screen_number)
{
  gchar *name = g_strdup_printf ("_NET_WM_CM_S%d", screen_number);
  GdkAtom atom = gdk_atom_intern (name, FALSE);
  g_free (name);
  return atom;
}

static void
init_monitor_geometry (GdkX11Monitor *monitor,
		       int x, int y, int width, int height)
{
  monitor->geometry.x = x;
  monitor->geometry.y = y;
  monitor->geometry.width = width;
  monitor->geometry.height = height;

  monitor->output = None;
  monitor->width_mm = -1;
  monitor->height_mm = -1;
  monitor->output_name = NULL;
  monitor->manufacturer = NULL;
}

static gboolean
init_fake_xinerama (GdkScreen *screen)
{
#ifdef G_ENABLE_DEBUG
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  XSetWindowAttributes atts;
  Window win;
  gint w, h;

  if (!(_gdk_debug_flags & GDK_DEBUG_XINERAMA))
    return FALSE;
  
  /* Fake Xinerama mode by splitting the screen into 4 monitors.
   * Also draw a little cross to make the monitor boundaries visible.
   */
  w = WidthOfScreen (x11_screen->xscreen);
  h = HeightOfScreen (x11_screen->xscreen);

  x11_screen->n_monitors = 4;
  x11_screen->monitors = g_new0 (GdkX11Monitor, 4);
  init_monitor_geometry (&x11_screen->monitors[0], 0, 0, w / 2, h / 2);
  init_monitor_geometry (&x11_screen->monitors[1], w / 2, 0, w / 2, h / 2);
  init_monitor_geometry (&x11_screen->monitors[2], 0, h / 2, w / 2, h / 2);
  init_monitor_geometry (&x11_screen->monitors[3], w / 2, h / 2, w / 2, h / 2);
  
  atts.override_redirect = 1;
  atts.background_pixel = WhitePixel(GDK_SCREEN_XDISPLAY (screen), 
				     x11_screen->screen_num);
  win = XCreateWindow(GDK_SCREEN_XDISPLAY (screen), 
		      x11_screen->xroot_window, 0, h / 2, w, 1, 0, 
		      DefaultDepth(GDK_SCREEN_XDISPLAY (screen), 
				   x11_screen->screen_num),
		      InputOutput, 
		      DefaultVisual(GDK_SCREEN_XDISPLAY (screen), 
				    x11_screen->screen_num),
		      CWOverrideRedirect|CWBackPixel, 
		      &atts);
  XMapRaised(GDK_SCREEN_XDISPLAY (screen), win); 
  win = XCreateWindow(GDK_SCREEN_XDISPLAY (screen), 
		      x11_screen->xroot_window, w/2 , 0, 1, h, 0, 
		      DefaultDepth(GDK_SCREEN_XDISPLAY (screen), 
				   x11_screen->screen_num),
		      InputOutput, 
		      DefaultVisual(GDK_SCREEN_XDISPLAY (screen), 
				    x11_screen->screen_num),
		      CWOverrideRedirect|CWBackPixel, 
		      &atts);
  XMapRaised(GDK_SCREEN_XDISPLAY (screen), win);
  return TRUE;
#endif
  
  return FALSE;
}

static void
free_monitors (GdkX11Monitor *monitors,
               gint           n_monitors)
{
  int i;

  for (i = 0; i < n_monitors; ++i)
    {
      g_free (monitors[i].output_name);
      g_free (monitors[i].manufacturer);
    }

  g_free (monitors);
}

#ifdef HAVE_RANDR
static int
monitor_compare_function (GdkX11Monitor *monitor1,
                          GdkX11Monitor *monitor2)
{
  /* Sort the leftmost/topmost monitors first.
   * For "cloned" monitors, sort the bigger ones first
   * (giving preference to taller monitors over wider
   * monitors)
   */

  if (monitor1->geometry.x != monitor2->geometry.x)
    return monitor1->geometry.x - monitor2->geometry.x;

  if (monitor1->geometry.y != monitor2->geometry.y)
    return monitor1->geometry.y - monitor2->geometry.y;

  if (monitor1->geometry.height != monitor2->geometry.height)
    return - (monitor1->geometry.height - monitor2->geometry.height);

  if (monitor1->geometry.width != monitor2->geometry.width)
    return - (monitor1->geometry.width - monitor2->geometry.width);

  return 0;
}
#endif

static gboolean
init_randr13 (GdkScreen *screen)
{
#ifdef HAVE_RANDR
  GdkDisplay *display = gdk_screen_get_display (screen);
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  Display *dpy = GDK_SCREEN_XDISPLAY (screen);
  XRRScreenResources *resources;
  RROutput primary_output;
  RROutput first_output = None;
  int i;
  GArray *monitors;
  gboolean randr12_compat = FALSE;

  if (!display_x11->have_randr13)
      return FALSE;

  resources = XRRGetScreenResourcesCurrent (x11_screen->xdisplay,
				            x11_screen->xroot_window);
  if (!resources)
    return FALSE;

  monitors = g_array_sized_new (FALSE, TRUE, sizeof (GdkX11Monitor),
                                resources->noutput);

  for (i = 0; i < resources->noutput; ++i)
    {
      XRROutputInfo *output =
	XRRGetOutputInfo (dpy, resources, resources->outputs[i]);

      /* Non RandR1.2 X driver have output name "default" */
      randr12_compat |= !g_strcmp0 (output->name, "default");

      if (output->connection == RR_Disconnected)
        {
          XRRFreeOutputInfo (output);
          continue;
        }

      if (output->crtc)
	{
	  GdkX11Monitor monitor;
	  XRRCrtcInfo *crtc = XRRGetCrtcInfo (dpy, resources, output->crtc);

	  monitor.geometry.x = crtc->x;
	  monitor.geometry.y = crtc->y;
	  monitor.geometry.width = crtc->width;
	  monitor.geometry.height = crtc->height;

	  monitor.output = resources->outputs[i];
	  monitor.width_mm = output->mm_width;
	  monitor.height_mm = output->mm_height;
	  monitor.output_name = g_strdup (output->name);
	  /* FIXME: need EDID parser */
	  monitor.manufacturer = NULL;

	  g_array_append_val (monitors, monitor);

          XRRFreeCrtcInfo (crtc);
	}

      XRRFreeOutputInfo (output);
    }

  if (resources->noutput > 0)
    first_output = resources->outputs[0];

  XRRFreeScreenResources (resources);

  /* non RandR 1.2 X driver doesn't return any usable multihead data */
  if (randr12_compat)
    {
      guint n_monitors = monitors->len;

      free_monitors ((GdkX11Monitor *)g_array_free (monitors, FALSE),
		     n_monitors);

      return FALSE;
    }

  g_array_sort (monitors,
                (GCompareFunc) monitor_compare_function);
  x11_screen->n_monitors = monitors->len;
  x11_screen->monitors = (GdkX11Monitor *)g_array_free (monitors, FALSE);

  x11_screen->primary_monitor = 0;

  primary_output = XRRGetOutputPrimary (x11_screen->xdisplay,
                                        x11_screen->xroot_window);

  for (i = 0; i < x11_screen->n_monitors; ++i)
    {
      if (x11_screen->monitors[i].output == primary_output)
	{
	  x11_screen->primary_monitor = i;
	  break;
	}

      /* No RandR1.3+ available or no primary set, fall back to prefer LVDS as primary if present */
      if (primary_output == None &&
          g_ascii_strncasecmp (x11_screen->monitors[i].output_name, "LVDS", 4) == 0)
	{
	  x11_screen->primary_monitor = i;
	  break;
	}

      /* No primary specified and no LVDS found */
      if (x11_screen->monitors[i].output == first_output)
	x11_screen->primary_monitor = i;
    }

  return x11_screen->n_monitors > 0;
#endif

  return FALSE;
}

static gboolean
init_solaris_xinerama (GdkScreen *screen)
{
#ifdef HAVE_SOLARIS_XINERAMA
  Display *dpy = GDK_SCREEN_XDISPLAY (screen);
  int screen_no = gdk_screen_get_number (screen);
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  XRectangle monitors[MAXFRAMEBUFFERS];
  unsigned char hints[16];
  gint result;
  int n_monitors;
  int i;
  
  if (!XineramaGetState (dpy, screen_no))
    return FALSE;

  result = XineramaGetInfo (dpy, screen_no, monitors, hints, &n_monitors);

  /* Yes I know it should be Success but the current implementation 
   * returns the num of monitor
   */
  if (result == 0)
    {
      return FALSE;
    }

  x11_screen->monitors = g_new0 (GdkX11Monitor, n_monitors);
  x11_screen->n_monitors = n_monitors;

  for (i = 0; i < n_monitors; i++)
    {
      init_monitor_geometry (&x11_screen->monitors[i],
			     monitors[i].x, monitors[i].y,
			     monitors[i].width, monitors[i].height);
    }

  x11_screen->primary_monitor = 0;

  return TRUE;
#endif /* HAVE_SOLARIS_XINERAMA */

  return FALSE;
}

static gboolean
init_xfree_xinerama (GdkScreen *screen)
{
#ifdef HAVE_XFREE_XINERAMA
  Display *dpy = GDK_SCREEN_XDISPLAY (screen);
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  XineramaScreenInfo *monitors;
  int i, n_monitors;
  
  if (!XineramaIsActive (dpy))
    return FALSE;

  monitors = XineramaQueryScreens (dpy, &n_monitors);
  
  if (n_monitors <= 0 || monitors == NULL)
    {
      /* If Xinerama doesn't think we have any monitors, try acting as
       * though we had no Xinerama. If the "no monitors" condition
       * is because XRandR 1.2 is currently switching between CRTCs,
       * we'll be notified again when we have our monitor back,
       * and can go back into Xinerama-ish mode at that point.
       */
      if (monitors)
	XFree (monitors);
      
      return FALSE;
    }

  x11_screen->n_monitors = n_monitors;
  x11_screen->monitors = g_new0 (GdkX11Monitor, n_monitors);
  
  for (i = 0; i < n_monitors; ++i)
    {
      init_monitor_geometry (&x11_screen->monitors[i],
			     monitors[i].x_org, monitors[i].y_org,
			     monitors[i].width, monitors[i].height);
    }
  
  XFree (monitors);
  
  x11_screen->primary_monitor = 0;

  return TRUE;
#endif /* HAVE_XFREE_XINERAMA */
  
  return FALSE;
}

static void
deinit_multihead (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  free_monitors (x11_screen->monitors, x11_screen->n_monitors);

  x11_screen->n_monitors = 0;
  x11_screen->monitors = NULL;
}

static gboolean
compare_monitor (GdkX11Monitor *m1,
                 GdkX11Monitor *m2)
{
  if (m1->geometry.x != m2->geometry.x ||
      m1->geometry.y != m2->geometry.y ||
      m1->geometry.width != m2->geometry.width ||
      m1->geometry.height != m2->geometry.height)
    return FALSE;

  if (m1->width_mm != m2->width_mm ||
      m1->height_mm != m2->height_mm)
    return FALSE;

  if (g_strcmp0 (m1->output_name, m2->output_name) != 0)
    return FALSE;

  if (g_strcmp0 (m1->manufacturer, m2->manufacturer) != 0)
    return FALSE;

  return TRUE;
}

static gboolean
compare_monitors (GdkX11Monitor *monitors1, gint n_monitors1,
                  GdkX11Monitor *monitors2, gint n_monitors2)
{
  gint i;

  if (n_monitors1 != n_monitors2)
    return FALSE;

  for (i = 0; i < n_monitors1; i++)
    {
      if (!compare_monitor (monitors1 + i, monitors2 + i))
        return FALSE;
    }

  return TRUE;
}

static void
init_multihead (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  int opcode, firstevent, firsterror;

  /* There are four different implementations of multihead support: 
   *
   *  1. Fake Xinerama for debugging purposes
   *  2. RandR 1.2
   *  3. Solaris Xinerama
   *  4. XFree86/Xorg Xinerama
   *
   * We use them in that order.
   */
  if (init_fake_xinerama (screen))
    return;

  if (init_randr13 (screen))
    return;

  if (XQueryExtension (GDK_SCREEN_XDISPLAY (screen), "XINERAMA",
		       &opcode, &firstevent, &firsterror))
    {
      if (init_solaris_xinerama (screen))
	return;
      
      if (init_xfree_xinerama (screen))
	return;
    }

  /* No multihead support of any kind for this screen */
  x11_screen->n_monitors = 1;
  x11_screen->monitors = g_new0 (GdkX11Monitor, 1);
  x11_screen->primary_monitor = 0;

  init_monitor_geometry (x11_screen->monitors, 0, 0,
			 WidthOfScreen (x11_screen->xscreen),
			 HeightOfScreen (x11_screen->xscreen));
}

GdkScreen *
_gdk_x11_screen_new (GdkDisplay *display,
		     gint	 screen_number) 
{
  GdkScreen *screen;
  GdkX11Screen *x11_screen;
  GdkX11Display *display_x11 = GDK_X11_DISPLAY (display);

  screen = g_object_new (GDK_TYPE_X11_SCREEN, NULL);

  x11_screen = GDK_X11_SCREEN (screen);
  x11_screen->display = display;
  x11_screen->xdisplay = display_x11->xdisplay;
  x11_screen->xscreen = ScreenOfDisplay (display_x11->xdisplay, screen_number);
  x11_screen->screen_num = screen_number;
  x11_screen->xroot_window = RootWindow (display_x11->xdisplay,screen_number);
  x11_screen->wmspec_check_window = None;
  /* we want this to be always non-null */
  x11_screen->window_manager_name = g_strdup ("unknown");
  
  init_multihead (screen);
  init_randr_support (screen);
  
  _gdk_x11_screen_init_visuals (screen);
  _gdk_x11_screen_init_root_window (screen);
  
  return screen;
}

/*
 * It is important that we first request the selection
 * notification, and then setup the initial state of
 * is_composited to avoid a race condition here.
 */
void
_gdk_x11_screen_setup (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  x11_screen->cm_selection_atom = make_cm_atom (x11_screen->screen_num);
  gdk_display_request_selection_notification (x11_screen->display,
					      x11_screen->cm_selection_atom);
  x11_screen->is_composited = check_is_composited (x11_screen->display, x11_screen);
}

static gboolean
gdk_x11_screen_is_composited (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);

  x11_screen = GDK_X11_SCREEN (screen);

  return x11_screen->is_composited;
}

static void
init_randr_support (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  XSelectInput (GDK_SCREEN_XDISPLAY (screen),
                x11_screen->xroot_window,
                StructureNotifyMask);

#ifdef HAVE_RANDR
  if (!GDK_X11_DISPLAY (gdk_screen_get_display (screen))->have_randr12)
    return;

  XRRSelectInput (GDK_SCREEN_XDISPLAY (screen),
                  x11_screen->xroot_window,
                  RRScreenChangeNotifyMask
                  | RRCrtcChangeNotifyMask
                  | RROutputPropertyNotifyMask);
#endif
}

static void
process_monitors_change (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  gint		 n_monitors;
  gint		 primary_monitor;
  GdkX11Monitor	*monitors;
  gboolean changed;

  primary_monitor = x11_screen->primary_monitor;
  n_monitors = x11_screen->n_monitors;
  monitors = x11_screen->monitors;

  x11_screen->n_monitors = 0;
  x11_screen->monitors = NULL;

  init_multihead (screen);

  changed =
    !compare_monitors (monitors, n_monitors,
		       x11_screen->monitors, x11_screen->n_monitors) ||
    x11_screen->primary_monitor != primary_monitor;


  free_monitors (monitors, n_monitors);

  if (changed)
    g_signal_emit_by_name (screen, "monitors-changed");
}

void
_gdk_x11_screen_size_changed (GdkScreen *screen,
			      XEvent    *event)
{
  gint width, height;
#ifdef HAVE_RANDR
  GdkX11Display *display_x11;
#endif

  width = gdk_screen_get_width (screen);
  height = gdk_screen_get_height (screen);

#ifdef HAVE_RANDR
  display_x11 = GDK_X11_DISPLAY (gdk_screen_get_display (screen));

  if (display_x11->have_randr13 && event->type == ConfigureNotify)
    return;

  XRRUpdateConfiguration (event);
#else
  if (event->type == ConfigureNotify)
    {
      XConfigureEvent *rcevent = (XConfigureEvent *) event;
      Screen	    *xscreen = gdk_x11_screen_get_xscreen (screen);

      xscreen->width   = rcevent->width;
      xscreen->height  = rcevent->height;
    }
  else
    return;
#endif

  process_monitors_change (screen);

  if (width != gdk_screen_get_width (screen) ||
      height != gdk_screen_get_height (screen))
    g_signal_emit_by_name (screen, "size-changed");
}

void
_gdk_x11_screen_window_manager_changed (GdkScreen *screen)
{
  g_signal_emit (screen, signals[WINDOW_MANAGER_CHANGED], 0);
}

void
_gdk_x11_screen_process_owner_change (GdkScreen *screen,
				      XEvent *event)
{
#ifdef HAVE_XFIXES
  XFixesSelectionNotifyEvent *selection_event = (XFixesSelectionNotifyEvent *)event;
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);
  Atom xcm_selection_atom = gdk_x11_atom_to_xatom_for_display (x11_screen->display,
							       x11_screen->cm_selection_atom);

  if (selection_event->selection == xcm_selection_atom)
    {
      gboolean composited = selection_event->owner != None;

      if (composited != x11_screen->is_composited)
	{
	  x11_screen->is_composited = composited;

	  g_signal_emit_by_name (screen, "composited-changed");
	}
    }
#endif
}

static gchar *
substitute_screen_number (const gchar *display_name,
                          gint         screen_number)
{
  GString *str;
  gchar   *p;

  str = g_string_new (display_name);

  p = strrchr (str->str, '.');
  if (p && p >	strchr (str->str, ':'))
    g_string_truncate (str, p - str->str);

  g_string_append_printf (str, ".%d", screen_number);

  return g_string_free (str, FALSE);
}

static gchar *
gdk_x11_screen_make_display_name (GdkScreen *screen)
{
  const gchar *old_display;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  old_display = gdk_display_get_name (gdk_screen_get_display (screen));

  return substitute_screen_number (old_display,
                                   gdk_screen_get_number (screen));
}

static GdkWindow *
gdk_x11_screen_get_active_window (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;
  GdkWindow *ret = NULL;
  Atom type_return;
  gint format_return;
  gulong nitems_return;
  gulong bytes_after_return;
  guchar *data = NULL;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  if (!gdk_x11_screen_supports_net_wm_hint (screen,
                                            gdk_atom_intern_static_string ("_NET_ACTIVE_WINDOW")))
    return NULL;

  x11_screen = GDK_X11_SCREEN (screen);

  if (XGetWindowProperty (x11_screen->xdisplay, x11_screen->xroot_window,
	                  gdk_x11_get_xatom_by_name_for_display (x11_screen->display,
			                                         "_NET_ACTIVE_WINDOW"),
		          0, 1, False, XA_WINDOW, &type_return,
		          &format_return, &nitems_return,
                          &bytes_after_return, &data)
      == Success)
    {
      if ((type_return == XA_WINDOW) && (format_return == 32) && (data))
        {
          Window window = *(Window *) data;

          if (window != None)
            {
              ret = gdk_x11_window_foreign_new_for_display (x11_screen->display,
                                                            window);
            }
        }
    }

  if (data)
    XFree (data);

  return ret;
}

static GList *
gdk_x11_screen_get_window_stack (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;
  GList *ret = NULL;
  Atom type_return;
  gint format_return;
  gulong nitems_return;
  gulong bytes_after_return;
  guchar *data = NULL;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

  if (!gdk_x11_screen_supports_net_wm_hint (screen,
                                            gdk_atom_intern_static_string ("_NET_CLIENT_LIST_STACKING")))
    return NULL;

  x11_screen = GDK_X11_SCREEN (screen);

  if (XGetWindowProperty (x11_screen->xdisplay, x11_screen->xroot_window,
	                  gdk_x11_get_xatom_by_name_for_display (x11_screen->display,
			                                         "_NET_CLIENT_LIST_STACKING"),
		          0, G_MAXLONG, False, XA_WINDOW, &type_return,
		          &format_return, &nitems_return,
                          &bytes_after_return, &data)
      == Success)
    {
      if ((type_return == XA_WINDOW) && (format_return == 32) &&
          (data) && (nitems_return > 0))
        {
          gulong *stack = (gulong *) data;
          GdkWindow *win;
          int i;

          for (i = 0; i < nitems_return; i++)
            {
              win = gdk_x11_window_foreign_new_for_display (x11_screen->display,
                                                            (Window)stack[i]);

              if (win != NULL)
                ret = g_list_append (ret, win);
            }
        }
    }

  if (data)
    XFree (data);

  return ret;
}

static gboolean
check_transform (const gchar *xsettings_name,
		 GType        src_type,
		 GType        dest_type)
{
  if (!g_value_type_transformable (src_type, dest_type))
    {
      g_warning ("Cannot transform xsetting %s of type %s to type %s\n",
		 xsettings_name,
		 g_type_name (src_type),
		 g_type_name (dest_type));
      return FALSE;
    }
  else
    return TRUE;
}

static gboolean
gdk_x11_screen_get_setting (GdkScreen   *screen,
			    const gchar *name,
			    GValue      *value)
{

  const char *xsettings_name = NULL;
  XSettingsResult result;
  XSettingsSetting *setting = NULL;
  GdkX11Screen *x11_screen;
  gboolean success = FALSE;
  gint i;
  GValue tmp_val = { 0, };

  g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);

  x11_screen = GDK_X11_SCREEN (screen);

  for (i = 0; i < GDK_SETTINGS_N_ELEMENTS(); i++)
    if (strcmp (GDK_SETTINGS_GDK_NAME (i), name) == 0)
      {
	xsettings_name = GDK_SETTINGS_X_NAME (i);
	break;
      }

  if (!xsettings_name)
    goto out;

  result = xsettings_client_get_setting (x11_screen->xsettings_client,
					 xsettings_name, &setting);
  if (result != XSETTINGS_SUCCESS)
    goto out;

  switch (setting->type)
    {
    case XSETTINGS_TYPE_INT:
      if (check_transform (xsettings_name, G_TYPE_INT, G_VALUE_TYPE (value)))
	{
	  g_value_init (&tmp_val, G_TYPE_INT);
	  g_value_set_int (&tmp_val, setting->data.v_int);
	  g_value_transform (&tmp_val, value);

	  success = TRUE;
	}
      break;
    case XSETTINGS_TYPE_STRING:
      if (check_transform (xsettings_name, G_TYPE_STRING, G_VALUE_TYPE (value)))
	{
	  g_value_init (&tmp_val, G_TYPE_STRING);
	  g_value_set_string (&tmp_val, setting->data.v_string);
	  g_value_transform (&tmp_val, value);

	  success = TRUE;
	}
      break;
    case XSETTINGS_TYPE_COLOR:
      if (!check_transform (xsettings_name, GDK_TYPE_COLOR, G_VALUE_TYPE (value)))
	{
	  GdkColor color;

	  g_value_init (&tmp_val, GDK_TYPE_COLOR);

	  color.pixel = 0;
	  color.red = setting->data.v_color.red;
	  color.green = setting->data.v_color.green;
	  color.blue = setting->data.v_color.blue;

	  g_value_set_boxed (&tmp_val, &color);

	  g_value_transform (&tmp_val, value);

	  success = TRUE;
	}
      break;
    }

  g_value_unset (&tmp_val);

 out:
  if (setting)
    xsettings_setting_free (setting);

  if (success)
    return TRUE;
  else
    return _gdk_x11_get_xft_setting (screen, name, value);
}

static void
cleanup_atoms(gpointer data)
{
  NetWmSupportedAtoms *supported_atoms = data;
  if (supported_atoms->atoms)
      XFree (supported_atoms->atoms);
  g_free (supported_atoms);
}

static Window
get_net_supporting_wm_check (GdkX11Screen *screen,
                             Window        window)
{
  GdkDisplay *display;
  Atom type;
  gint format;
  gulong n_items;
  gulong bytes_after;
  guchar *data;
  Window value;

  display = screen->display;
  type = None;
  data = NULL;
  value = None;

  gdk_x11_display_error_trap_push (display);
  XGetWindowProperty (screen->xdisplay, window,
		      gdk_x11_get_xatom_by_name_for_display (display, "_NET_SUPPORTING_WM_CHECK"),
		      0, G_MAXLONG, False, XA_WINDOW, &type, &format,
		      &n_items, &bytes_after, &data);
  gdk_x11_display_error_trap_pop_ignored (display);

  if (type == XA_WINDOW)
    value = *(Window *)data;

  if (data)
    XFree (data);

  return value;
}

static void
fetch_net_wm_check_window (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;
  GdkDisplay *display;
  Window window;
  GTimeVal tv;
  gint error;

  x11_screen = GDK_X11_SCREEN (screen);
  display = x11_screen->display;

  g_return_if_fail (GDK_X11_DISPLAY (display)->trusted_client);

  if (x11_screen->wmspec_check_window != None)
    return; /* already have it */

  g_get_current_time (&tv);

  if (ABS  (tv.tv_sec - x11_screen->last_wmspec_check_time) < 15)
    return; /* we've checked recently */

  window = get_net_supporting_wm_check (x11_screen, x11_screen->xroot_window);
  if (window == None)
    return;

  if (window != get_net_supporting_wm_check (x11_screen, window))
    return;

  gdk_x11_display_error_trap_push (display);

  /* Find out if this WM goes away, so we can reset everything. */
  XSelectInput (x11_screen->xdisplay, window, StructureNotifyMask);

  error = gdk_x11_display_error_trap_pop (display);
  if (!error)
    {
      /* We check the window property again because after XGetWindowProperty()
       * and before XSelectInput() the window may have been recycled in such a
       * way that XSelectInput() doesn't fail but the window is no longer what
       * we want.
       */
      if (window != get_net_supporting_wm_check (x11_screen, window))
        return;

      x11_screen->wmspec_check_window = window;
      x11_screen->last_wmspec_check_time = tv.tv_sec;
      x11_screen->need_refetch_net_supported = TRUE;
      x11_screen->need_refetch_wm_name = TRUE;

      /* Careful, reentrancy */
      _gdk_x11_screen_window_manager_changed (screen);
    }
}

/**
 * gdk_x11_screen_supports_net_wm_hint:
 * @screen: (type GdkX11Screen): the relevant #GdkScreen.
 * @property: a property atom.
 *
 * This function is specific to the X11 backend of GDK, and indicates
 * whether the window manager supports a certain hint from the
 * Extended Window Manager Hints Specification. You can find this
 * specification on
 * <ulink url="http://www.freedesktop.org">http://www.freedesktop.org</ulink>.
 *
 * When using this function, keep in mind that the window manager
 * can change over time; so you shouldn't use this function in
 * a way that impacts persistent application state. A common bug
 * is that your application can start up before the window manager
 * does when the user logs in, and before the window manager starts
 * gdk_x11_screen_supports_net_wm_hint() will return %FALSE for every property.
 * You can monitor the window_manager_changed signal on #GdkScreen to detect
 * a window manager change.
 *
 * Return value: %TRUE if the window manager supports @property
 *
 * Since: 2.2
 **/
gboolean
gdk_x11_screen_supports_net_wm_hint (GdkScreen *screen,
				     GdkAtom    property)
{
  gulong i;
  GdkX11Screen *x11_screen;
  NetWmSupportedAtoms *supported_atoms;
  GdkDisplay *display;

  g_return_val_if_fail (GDK_IS_SCREEN (screen), FALSE);

  x11_screen = GDK_X11_SCREEN (screen);
  display = x11_screen->display;

  if (!G_LIKELY (GDK_X11_DISPLAY (display)->trusted_client))
    return FALSE;

  supported_atoms = g_object_get_data (G_OBJECT (screen), "gdk-net-wm-supported-atoms");
  if (!supported_atoms)
    {
      supported_atoms = g_new0 (NetWmSupportedAtoms, 1);
      g_object_set_data_full (G_OBJECT (screen), "gdk-net-wm-supported-atoms", supported_atoms, cleanup_atoms);
    }

  fetch_net_wm_check_window (screen);

  if (x11_screen->wmspec_check_window == None)
    return FALSE;

  if (x11_screen->need_refetch_net_supported)
    {
      /* WM has changed since we last got the supported list,
       * refetch it.
       */
      Atom type;
      gint format;
      gulong bytes_after;

      x11_screen->need_refetch_net_supported = FALSE;

      if (supported_atoms->atoms)
        XFree (supported_atoms->atoms);

      supported_atoms->atoms = NULL;
      supported_atoms->n_atoms = 0;

      XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display), x11_screen->xroot_window,
                          gdk_x11_get_xatom_by_name_for_display (display, "_NET_SUPPORTED"),
                          0, G_MAXLONG, False, XA_ATOM, &type, &format,
                          &supported_atoms->n_atoms, &bytes_after,
                          (guchar **)&supported_atoms->atoms);

      if (type != XA_ATOM)
        return FALSE;
    }

  if (supported_atoms->atoms == NULL)
    return FALSE;

  i = 0;
  while (i < supported_atoms->n_atoms)
    {
      if (supported_atoms->atoms[i] == gdk_x11_atom_to_xatom_for_display (display, property))
        return TRUE;

      ++i;
    }

  return FALSE;
}

static void
refcounted_grab_server (Display *xdisplay)
{
  GdkDisplay *display = gdk_x11_lookup_xdisplay (xdisplay);

  gdk_x11_display_grab (display);
}

static void
refcounted_ungrab_server (Display *xdisplay)
{
  GdkDisplay *display = gdk_x11_lookup_xdisplay (xdisplay);

  gdk_x11_display_ungrab (display);
}

static GdkFilterReturn
gdk_xsettings_client_event_filter (GdkXEvent *xevent,
				   GdkEvent  *event,
				   gpointer   data)
{
  GdkX11Screen *screen = data;

  if (xsettings_client_process_event (screen->xsettings_client, (XEvent *)xevent))
    return GDK_FILTER_REMOVE;
  else
    return GDK_FILTER_CONTINUE;
}

static Bool
gdk_xsettings_watch_cb (Window   window,
			Bool	 is_start,
			long     mask,
			void    *cb_data)
{
  GdkWindow *gdkwin;
  GdkScreen *screen = cb_data;

  gdkwin = gdk_x11_window_lookup_for_display (gdk_screen_get_display (screen), window);

  if (is_start)
    {
      if (gdkwin)
	g_object_ref (gdkwin);
      else
	{
	  gdkwin = gdk_x11_window_foreign_new_for_display (gdk_screen_get_display (screen), window);
	  
	  /* gdk_window_foreign_new_for_display() can fail and return NULL if the
	   * window has already been destroyed.
	   */
	  if (!gdkwin)
	    return False;
	}

      gdk_window_add_filter (gdkwin, gdk_xsettings_client_event_filter, screen);
    }
  else
    {
      if (!gdkwin)
	{
	  /* gdkwin should not be NULL here, since if starting the watch succeeded
	   * we have a reference on the window. It might mean that the caller didn't
	   * remove the watch when it got a DestroyNotify event. Or maybe the
	   * caller ignored the return value when starting the watch failed.
	   */
	  g_warning ("gdk_xsettings_watch_cb(): Couldn't find window to unwatch");
	  return False;
	}
      
      gdk_window_remove_filter (gdkwin, gdk_xsettings_client_event_filter, screen);
      g_object_unref (gdkwin);
    }

  return True;
}

static void
gdk_xsettings_notify_cb (const char       *name,
			 XSettingsAction   action,
			 XSettingsSetting *setting,
			 void             *data)
{
  GdkEvent new_event;
  GdkScreen *screen = data;
  GdkX11Screen *x11_screen = data;
  int i;

  if (x11_screen->xsettings_in_init)
    return;
  
  new_event.type = GDK_SETTING;
  new_event.setting.window = gdk_screen_get_root_window (screen);
  new_event.setting.send_event = FALSE;
  new_event.setting.name = NULL;

  for (i = 0; i < GDK_SETTINGS_N_ELEMENTS() ; i++)
    if (strcmp (GDK_SETTINGS_X_NAME (i), name) == 0)
      {
	new_event.setting.name = (char*) GDK_SETTINGS_GDK_NAME (i);
	break;
      }
  
  if (!new_event.setting.name)
    return;
  
  switch (action)
    {
    case XSETTINGS_ACTION_NEW:
      new_event.setting.action = GDK_SETTING_ACTION_NEW;
      break;
    case XSETTINGS_ACTION_CHANGED:
      new_event.setting.action = GDK_SETTING_ACTION_CHANGED;
      break;
    case XSETTINGS_ACTION_DELETED:
      new_event.setting.action = GDK_SETTING_ACTION_DELETED;
      break;
    }

  gdk_event_put (&new_event);
}

void
_gdk_x11_screen_init_events (GdkScreen *screen)
{
  GdkX11Screen *x11_screen = GDK_X11_SCREEN (screen);

  /* Keep a flag to avoid extra notifies that we don't need
   */
  x11_screen->xsettings_in_init = TRUE;
  x11_screen->xsettings_client = xsettings_client_new_with_grab_funcs (x11_screen->xdisplay,
						                       x11_screen->screen_num,
						                       gdk_xsettings_notify_cb,
						                       gdk_xsettings_watch_cb,
						                       screen,
                                                                       refcounted_grab_server,
                                                                       refcounted_ungrab_server);
  x11_screen->xsettings_in_init = FALSE;
}

/**
 * gdk_x11_screen_get_window_manager_name:
 * @screen: (type GdkX11Screen): a #GdkScreen
 *
 * Returns the name of the window manager for @screen.
 *
 * Return value: the name of the window manager screen @screen, or
 * "unknown" if the window manager is unknown. The string is owned by GDK
 * and should not be freed.
 *
 * Since: 2.2
 **/
const char*
gdk_x11_screen_get_window_manager_name (GdkScreen *screen)
{
  GdkX11Screen *x11_screen;
  GdkDisplay *display;

  x11_screen = GDK_X11_SCREEN (screen);
  display = x11_screen->display;

  if (!G_LIKELY (GDK_X11_DISPLAY (display)->trusted_client))
    return x11_screen->window_manager_name;

  fetch_net_wm_check_window (screen);

  if (x11_screen->need_refetch_wm_name)
    {
      /* Get the name of the window manager */
      x11_screen->need_refetch_wm_name = FALSE;

      g_free (x11_screen->window_manager_name);
      x11_screen->window_manager_name = g_strdup ("unknown");

      if (x11_screen->wmspec_check_window != None)
        {
          Atom type;
          gint format;
          gulong n_items;
          gulong bytes_after;
          gchar *name;

          name = NULL;

          gdk_x11_display_error_trap_push (display);

          XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display),
                              x11_screen->wmspec_check_window,
                              gdk_x11_get_xatom_by_name_for_display (display,
                                                                     "_NET_WM_NAME"),
                              0, G_MAXLONG, False,
                              gdk_x11_get_xatom_by_name_for_display (display,
                                                                     "UTF8_STRING"),
                              &type, &format,
                              &n_items, &bytes_after,
                              (guchar **)&name);

          gdk_x11_display_error_trap_pop_ignored (display);

          if (name != NULL)
            {
              g_free (x11_screen->window_manager_name);
              x11_screen->window_manager_name = g_strdup (name);
              XFree (name);
            }
        }
    }

  return GDK_X11_SCREEN (screen)->window_manager_name;
}

static void
gdk_x11_screen_class_init (GdkX11ScreenClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkScreenClass *screen_class = GDK_SCREEN_CLASS (klass);

  object_class->dispose = gdk_x11_screen_dispose;
  object_class->finalize = gdk_x11_screen_finalize;

  screen_class->get_display = gdk_x11_screen_get_display;
  screen_class->get_width = gdk_x11_screen_get_width;
  screen_class->get_height = gdk_x11_screen_get_height;
  screen_class->get_width_mm = gdk_x11_screen_get_width_mm;
  screen_class->get_height_mm = gdk_x11_screen_get_height_mm;
  screen_class->get_number = gdk_x11_screen_get_number;
  screen_class->get_root_window = gdk_x11_screen_get_root_window;
  screen_class->get_n_monitors = gdk_x11_screen_get_n_monitors;
  screen_class->get_primary_monitor = gdk_x11_screen_get_primary_monitor;
  screen_class->get_monitor_width_mm = gdk_x11_screen_get_monitor_width_mm;
  screen_class->get_monitor_height_mm = gdk_x11_screen_get_monitor_height_mm;
  screen_class->get_monitor_plug_name = gdk_x11_screen_get_monitor_plug_name;
  screen_class->get_monitor_geometry = gdk_x11_screen_get_monitor_geometry;
  screen_class->get_system_visual = _gdk_x11_screen_get_system_visual;
  screen_class->get_rgba_visual = gdk_x11_screen_get_rgba_visual;
  screen_class->is_composited = gdk_x11_screen_is_composited;
  screen_class->make_display_name = gdk_x11_screen_make_display_name;
  screen_class->get_active_window = gdk_x11_screen_get_active_window;
  screen_class->get_window_stack = gdk_x11_screen_get_window_stack;
  screen_class->get_setting = gdk_x11_screen_get_setting;
  screen_class->visual_get_best_depth = _gdk_x11_screen_visual_get_best_depth;
  screen_class->visual_get_best_type = _gdk_x11_screen_visual_get_best_type;
  screen_class->visual_get_best = _gdk_x11_screen_visual_get_best;
  screen_class->visual_get_best_with_depth = _gdk_x11_screen_visual_get_best_with_depth;
  screen_class->visual_get_best_with_type = _gdk_x11_screen_visual_get_best_with_type;
  screen_class->visual_get_best_with_both = _gdk_x11_screen_visual_get_best_with_both;
  screen_class->query_depths = _gdk_x11_screen_query_depths;
  screen_class->query_visual_types = _gdk_x11_screen_query_visual_types;
  screen_class->list_visuals = _gdk_x11_screen_list_visuals;

  signals[WINDOW_MANAGER_CHANGED] =
    g_signal_new (g_intern_static_string ("window_manager_changed"),
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GdkX11ScreenClass, window_manager_changed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);
}
