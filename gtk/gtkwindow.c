/* GTK - The GIMP Toolkit
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

#include "gtkwindow.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "gtkprivate.h"
#include "gtkwindowprivate.h"
#include "gtkaccelgroupprivate.h"
#include "gtkbindings.h"
#include "gtkkeyhash.h"
#include "gtkmain.h"
#include "gtkmnemonichash.h"
#include "gtkmenubar.h"
#include "gtkiconfactory.h"
#include "gtkicontheme.h"
#include "gtkmarshalers.h"
#include "gtkplug.h"
#include "gtkbuildable.h"
#include "gtkwidgetprivate.h"
#include "gtkcontainerprivate.h"
#include "gtkintl.h"
#include "gtkstylecontextprivate.h"
#include "gtktypebuiltins.h"
#include "a11y/gtkwindowaccessible.h"
#include "gtkstyle.h"

#ifdef GDK_WINDOWING_X11
#include "x11/gdkx.h"
#endif

/**
 * SECTION:gtkwindow
 * @title: GtkWindow
 * @short_description: Toplevel which can contain other widgets
 *
 * A GtkWindow is a toplevel window which can contain other widgets.
 * Windows normally have decorations that are under the control
 * of the windowing system and allow the user to manipulate the window
 * (resize it, move it, close it,...).
 *
 * GTK+ also allows windows to have a resize grip (a small area in the lower
 * right or left corner) which can be clicked to reszie the window. To
 * control whether a window has a resize grip, use
 * gtk_window_set_has_resize_grip().
 *
 * <refsect2 id="GtkWindow-BUILDER-UI">
 * <title>GtkWindow as GtkBuildable</title>
 * <para>
 * The GtkWindow implementation of the GtkBuildable interface supports a
 * custom <tag class="starttag">accel-groups</tag> element, which supports
 * any number of <tag class="starttag">group</tag> elements representing the
 * #GtkAccelGroup objects you want to add to your window (synonymous with
 * gtk_window_add_accel_group().
 * </para>
 * <example>
 * <title>A UI definition fragment with accel groups</title>
 * <programlisting><![CDATA[
 * <object class="GtkWindow">
 *   <accel-groups>
 *     <group name="accelgroup1"/>
 *   </accel-groups>
 * </object>
 * <!-- -->
 * ...
 * <!-- -->
 * <object class="GtkAccelGroup" id="accelgroup1"/>
 * ]]></programlisting>
 * </example>
 * </refsect2>
 */

#define AUTO_MNEMONICS_DELAY 300 /* ms */

typedef struct _GtkDeviceGrabInfo GtkDeviceGrabInfo;

struct _GtkWindowPrivate
{
  GtkMnemonicHash       *mnemonic_hash;

  GtkWidget             *attach_widget;
  GtkWidget             *default_widget;
  GtkWidget             *focus_widget;
  GtkWindow             *transient_parent;
  GtkWindowGeometryInfo *geometry_info;
  GtkWindowGroup        *group;
  GdkScreen             *screen;
  GtkApplication        *application;

  GdkModifierType        mnemonic_modifier;
  GdkWindowTypeHint      gdk_type_hint;

  gdouble  opacity;

  GdkWindow *grip_window;

  gchar   *startup_id;
  gchar   *title;
  gchar   *wmclass_class;
  gchar   *wmclass_name;
  gchar   *wm_role;

  guint    keys_changed_handler;

  guint32  initial_timestamp;

  guint16  configure_request_count;

  guint    auto_mnemonics_timeout_id;

  /* The following flags are initially TRUE (before a window is mapped).
   * They cause us to compute a configure request that involves
   * default-only parameters. Once mapped, we set them to FALSE.
   * Then we set them to TRUE again on unmap (for position)
   * and on unrealize (for size).
   */
  guint    need_default_position     : 1;
  guint    need_default_size         : 1;

  guint    above_initially           : 1;
  guint    accept_focus              : 1;
  guint    below_initially           : 1;
  guint    builder_visible           : 1;
  guint    configure_notify_received : 1;
  guint    decorated                 : 1;
  guint    deletable                 : 1;
  guint    destroy_with_parent       : 1;
  guint    focus_on_map              : 1;
  guint    fullscreen_initially      : 1;
  guint    has_focus                 : 1;
  guint    has_user_ref_count        : 1;
  guint    has_toplevel_focus        : 1;
  guint    hide_titlebar_when_maximized : 1;
  guint    iconify_initially         : 1; /* gtk_window_iconify() called before realization */
  guint    is_active                 : 1;
  guint    maximize_initially        : 1;
  guint    mnemonics_visible         : 1;
  guint    mnemonics_visible_set     : 1;
  guint    focus_visible             : 1;
  guint    modal                     : 1;
  guint    opacity_set               : 1;
  guint    position                  : 3;
  guint    reset_type_hint           : 1;
  guint    resizable                 : 1;
  guint    skips_pager               : 1;
  guint    skips_taskbar             : 1;
  guint    stick_initially           : 1;
  guint    transient_parent_group    : 1;
  guint    type                      : 4; /* GtkWindowType */
  guint    type_hint                 : 3; /* GdkWindowTypeHint if the hint is
                                           * one of the original eight. If not,
                                           * then it contains
                                           * GDK_WINDOW_TYPE_HINT_NORMAL
                                           */
  guint    urgent                    : 1;
  guint    has_resize_grip           : 1;
  guint    resize_grip_visible       : 1;  /* don't use, just for "resize-
                                            * grip-visible" notification
                                            */
  guint    gravity                   : 5; /* GdkGravity */

};

enum {
  SET_FOCUS,
  FRAME_EVENT,
  ACTIVATE_FOCUS,
  ACTIVATE_DEFAULT,
  KEYS_CHANGED,
  LAST_SIGNAL
};

enum {
  PROP_0,

  /* Construct */
  PROP_TYPE,

  /* Normal Props */
  PROP_TITLE,
  PROP_ROLE,
  PROP_RESIZABLE,
  PROP_MODAL,
  PROP_WIN_POS,
  PROP_DEFAULT_WIDTH,
  PROP_DEFAULT_HEIGHT,
  PROP_DESTROY_WITH_PARENT,
  PROP_HIDE_TITLEBAR_WHEN_MAXIMIZED,
  PROP_ICON,
  PROP_ICON_NAME,
  PROP_SCREEN,
  PROP_TYPE_HINT,
  PROP_SKIP_TASKBAR_HINT,
  PROP_SKIP_PAGER_HINT,
  PROP_URGENCY_HINT,
  PROP_ACCEPT_FOCUS,
  PROP_FOCUS_ON_MAP,
  PROP_DECORATED,
  PROP_DELETABLE,
  PROP_GRAVITY,
  PROP_TRANSIENT_FOR,
  PROP_ATTACHED_TO,
  PROP_OPACITY,
  PROP_HAS_RESIZE_GRIP,
  PROP_RESIZE_GRIP_VISIBLE,
  PROP_APPLICATION,
  /* Readonly properties */
  PROP_IS_ACTIVE,
  PROP_HAS_TOPLEVEL_FOCUS,

  /* Writeonly properties */
  PROP_STARTUP_ID,

  PROP_MNEMONICS_VISIBLE,
  PROP_FOCUS_VISIBLE,

  LAST_ARG
};

typedef struct
{
  GList     *icon_list;
  gchar     *icon_name;
  guint      realized : 1;
  guint      using_default_icon : 1;
  guint      using_parent_icon : 1;
  guint      using_themed_icon : 1;
} GtkWindowIconInfo;

typedef struct {
  GdkGeometry    geometry; /* Last set of geometry hints we set */
  GdkWindowHints flags;
  GdkRectangle   configure_request;
} GtkWindowLastGeometryInfo;

struct _GtkWindowGeometryInfo
{
  /* Properties that the app has set on the window
   */
  GdkGeometry    geometry;	/* Geometry hints */
  GdkWindowHints mask;
  GtkWidget     *widget;	/* subwidget to which hints apply */
  /* from last gtk_window_resize () - if > 0, indicates that
   * we should resize to this size.
   */
  gint           resize_width;  
  gint           resize_height;

  /* From last gtk_window_move () prior to mapping -
   * only used if initial_pos_set
   */
  gint           initial_x;
  gint           initial_y;
  
  /* Default size - used only the FIRST time we map a window,
   * only if > 0.
   */
  gint           default_width; 
  gint           default_height;
  /* whether to use initial_x, initial_y */
  guint          initial_pos_set : 1;
  /* CENTER_ALWAYS or other position constraint changed since
   * we sent the last configure request.
   */
  guint          position_constraints_changed : 1;

  /* if true, default_width, height should be multiplied by the
   * increments and affect the geometry widget only
   */
  guint          default_is_geometry : 1;

  /* if true, resize_width, height should be multiplied by the
   * increments and affect the geometry widget only
   */
  guint          resize_is_geometry : 1;
  
  GtkWindowLastGeometryInfo last;
};


struct _GtkDeviceGrabInfo
{
  GtkWidget *widget;
  GdkDevice *device;
  guint block_others : 1;
};

struct _GtkWindowGroupPrivate
{
  GSList *grabs;
  GSList *device_grabs;
};

static void gtk_window_dispose            (GObject           *object);
static void gtk_window_finalize           (GObject           *object);
static void gtk_window_destroy            (GtkWidget         *widget);
static void gtk_window_show               (GtkWidget         *widget);
static void gtk_window_hide               (GtkWidget         *widget);
static void gtk_window_map                (GtkWidget         *widget);
static void gtk_window_unmap              (GtkWidget         *widget);
static void gtk_window_realize            (GtkWidget         *widget);
static void gtk_window_unrealize          (GtkWidget         *widget);
static void gtk_window_size_allocate      (GtkWidget         *widget,
					   GtkAllocation     *allocation);
static gboolean gtk_window_map_event      (GtkWidget         *widget,
                                           GdkEventAny       *event);
static gint gtk_window_configure_event    (GtkWidget         *widget,
					   GdkEventConfigure *event);
static gint gtk_window_key_press_event    (GtkWidget         *widget,
					   GdkEventKey       *event);
static gint gtk_window_key_release_event  (GtkWidget         *widget,
					   GdkEventKey       *event);
static gint gtk_window_button_press_event (GtkWidget         *widget,
                                           GdkEventButton    *event);
static gint gtk_window_enter_notify_event (GtkWidget         *widget,
					   GdkEventCrossing  *event);
static gint gtk_window_leave_notify_event (GtkWidget         *widget,
					   GdkEventCrossing  *event);
static gint gtk_window_focus_in_event     (GtkWidget         *widget,
					   GdkEventFocus     *event);
static gint gtk_window_focus_out_event    (GtkWidget         *widget,
					   GdkEventFocus     *event);
static void gtk_window_style_updated      (GtkWidget         *widget);
static gboolean gtk_window_state_event    (GtkWidget          *widget,
                                           GdkEventWindowState *event);
static void gtk_window_check_resize       (GtkContainer      *container);
static gint gtk_window_focus              (GtkWidget        *widget,
				           GtkDirectionType  direction);
static void gtk_window_move_focus         (GtkWidget         *widget,
                                           GtkDirectionType   dir);
static void gtk_window_real_set_focus     (GtkWindow         *window,
					   GtkWidget         *focus);
static void gtk_window_direction_changed  (GtkWidget         *widget,
                                           GtkTextDirection   prev_dir);
static void gtk_window_state_changed      (GtkWidget         *widget,
                                           GtkStateType       previous_state);

static void gtk_window_real_activate_default (GtkWindow         *window);
static void gtk_window_real_activate_focus   (GtkWindow         *window);
static void gtk_window_keys_changed          (GtkWindow         *window);
static gint gtk_window_draw                  (GtkWidget         *widget,
					      cairo_t           *cr);
static void gtk_window_unset_transient_for         (GtkWindow  *window);
static void gtk_window_transient_parent_realized   (GtkWidget  *parent,
						    GtkWidget  *window);
static void gtk_window_transient_parent_unrealized (GtkWidget  *parent,
						    GtkWidget  *window);

static GdkScreen *gtk_window_check_screen (GtkWindow *window);

static GtkWindowGeometryInfo* gtk_window_get_geometry_info         (GtkWindow    *window,
                                                                    gboolean      create);

static void     gtk_window_move_resize               (GtkWindow    *window);
static gboolean gtk_window_compare_hints             (GdkGeometry  *geometry_a,
                                                      guint         flags_a,
                                                      GdkGeometry  *geometry_b,
                                                      guint         flags_b);
static void     gtk_window_constrain_size            (GtkWindow    *window,
                                                      GdkGeometry  *geometry,
                                                      guint         flags,
                                                      gint          width,
                                                      gint          height,
                                                      gint         *new_width,
                                                      gint         *new_height);
static void     gtk_window_constrain_position        (GtkWindow    *window,
                                                      gint          new_width,
                                                      gint          new_height,
                                                      gint         *x,
                                                      gint         *y);
static void     gtk_window_compute_hints             (GtkWindow    *window,
                                                      GdkGeometry  *new_geometry,
                                                      guint        *new_flags);
static void     gtk_window_compute_configure_request (GtkWindow    *window,
                                                      GdkRectangle *request,
                                                      GdkGeometry  *geometry,
                                                      guint        *flags);

static void     gtk_window_set_default_size_internal (GtkWindow    *window,
                                                      gboolean      change_width,
                                                      gint          width,
                                                      gboolean      change_height,
                                                      gint          height,
						      gboolean      is_geometry);

static void     update_themed_icon                    (GtkIconTheme *theme,
				                       GtkWindow    *window);
static GList   *icon_list_from_theme                  (GtkWidget    *widget,
						       const gchar  *name);
static void     gtk_window_realize_icon               (GtkWindow    *window);
static void     gtk_window_unrealize_icon             (GtkWindow    *window);
static void     resize_grip_create_window             (GtkWindow    *window);
static void     resize_grip_destroy_window            (GtkWindow    *window);
static void     update_grip_visibility                (GtkWindow    *window);

static void        gtk_window_notify_keys_changed (GtkWindow   *window);
static GtkKeyHash *gtk_window_get_key_hash        (GtkWindow   *window);
static void        gtk_window_free_key_hash       (GtkWindow   *window);
static void	   gtk_window_on_composited_changed (GdkScreen *screen,
						     GtkWindow *window);
static void        gtk_window_on_theme_variant_changed (GtkSettings *settings,
                                                        GParamSpec  *pspec,
                                                        GtkWindow   *window);
static void        gtk_window_set_theme_variant         (GtkWindow  *window);

static GSList      *toplevel_list = NULL;
static guint        window_signals[LAST_SIGNAL] = { 0 };
static GList       *default_icon_list = NULL;
static gchar       *default_icon_name = NULL;
static guint        default_icon_serial = 0;
static gboolean     disable_startup_notification = FALSE;
static gboolean     sent_startup_notification = FALSE;

static GQuark       quark_gtk_embedded = 0;
static GQuark       quark_gtk_window_key_hash = 0;
static GQuark       quark_gtk_window_icon_info = 0;
static GQuark       quark_gtk_buildable_accels = 0;

static GtkBuildableIface *parent_buildable_iface;

static void gtk_window_set_property (GObject         *object,
				     guint            prop_id,
				     const GValue    *value,
				     GParamSpec      *pspec);
static void gtk_window_get_property (GObject         *object,
				     guint            prop_id,
				     GValue          *value,
				     GParamSpec      *pspec);

/* GtkBuildable */
static void gtk_window_buildable_interface_init  (GtkBuildableIface *iface);
static void gtk_window_buildable_set_buildable_property (GtkBuildable        *buildable,
							 GtkBuilder          *builder,
							 const gchar         *name,
							 const GValue        *value);
static void gtk_window_buildable_parser_finished (GtkBuildable     *buildable,
						  GtkBuilder       *builder);
static gboolean gtk_window_buildable_custom_tag_start (GtkBuildable  *buildable,
						       GtkBuilder    *builder,
						       GObject       *child,
						       const gchar   *tagname,
						       GMarkupParser *parser,
						       gpointer      *data);
static void gtk_window_buildable_custom_finished (GtkBuildable  *buildable,
						      GtkBuilder    *builder,
						      GObject       *child,
						      const gchar   *tagname,
						      gpointer       user_data);

static void ensure_state_flag_backdrop (GtkWidget *widget);

G_DEFINE_TYPE_WITH_CODE (GtkWindow, gtk_window, GTK_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE,
						gtk_window_buildable_interface_init))

static void
add_tab_bindings (GtkBindingSet    *binding_set,
		  GdkModifierType   modifiers,
		  GtkDirectionType  direction)
{
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Tab, modifiers,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Tab, modifiers,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
add_arrow_bindings (GtkBindingSet    *binding_set,
		    guint             keysym,
		    GtkDirectionType  direction)
{
  guint keypad_keysym = keysym - GDK_KEY_Left + GDK_KEY_KP_Left;
  
  gtk_binding_entry_add_signal (binding_set, keysym, 0,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keysym, GDK_CONTROL_MASK,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, 0,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, keypad_keysym, GDK_CONTROL_MASK,
                                "move-focus", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static guint32
extract_time_from_startup_id (const gchar* startup_id)
{
  gchar *timestr = g_strrstr (startup_id, "_TIME");
  guint32 retval = GDK_CURRENT_TIME;

  if (timestr)
    {
      gchar *end;
      guint32 timestamp; 
    
      /* Skip past the "_TIME" part */
      timestr += 5;

      end = NULL;
      errno = 0;
      timestamp = g_ascii_strtoull (timestr, &end, 0);
      if (errno == 0 && end != timestr)
        retval = timestamp;
    }

  return retval;
}

static gboolean
startup_id_is_fake (const gchar* startup_id)
{
  return strncmp (startup_id, "_TIME", 5) == 0;
}

static void
gtk_window_class_init (GtkWindowClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GtkBindingSet *binding_set;

  widget_class = (GtkWidgetClass*) klass;
  container_class = (GtkContainerClass*) klass;
  
  quark_gtk_embedded = g_quark_from_static_string ("gtk-embedded");
  quark_gtk_window_key_hash = g_quark_from_static_string ("gtk-window-key-hash");
  quark_gtk_window_icon_info = g_quark_from_static_string ("gtk-window-icon-info");
  quark_gtk_buildable_accels = g_quark_from_static_string ("gtk-window-buildable-accels");

  gobject_class->dispose = gtk_window_dispose;
  gobject_class->finalize = gtk_window_finalize;

  gobject_class->set_property = gtk_window_set_property;
  gobject_class->get_property = gtk_window_get_property;

  widget_class->destroy = gtk_window_destroy;
  widget_class->show = gtk_window_show;
  widget_class->hide = gtk_window_hide;
  widget_class->map = gtk_window_map;
  widget_class->map_event = gtk_window_map_event;
  widget_class->unmap = gtk_window_unmap;
  widget_class->realize = gtk_window_realize;
  widget_class->unrealize = gtk_window_unrealize;
  widget_class->size_allocate = gtk_window_size_allocate;
  widget_class->configure_event = gtk_window_configure_event;
  widget_class->key_press_event = gtk_window_key_press_event;
  widget_class->key_release_event = gtk_window_key_release_event;
  widget_class->enter_notify_event = gtk_window_enter_notify_event;
  widget_class->leave_notify_event = gtk_window_leave_notify_event;
  widget_class->focus_in_event = gtk_window_focus_in_event;
  widget_class->button_press_event = gtk_window_button_press_event;
  widget_class->focus_out_event = gtk_window_focus_out_event;
  widget_class->focus = gtk_window_focus;
  widget_class->move_focus = gtk_window_move_focus;
  widget_class->draw = gtk_window_draw;
  widget_class->window_state_event = gtk_window_state_event;
  widget_class->direction_changed = gtk_window_direction_changed;
  widget_class->state_changed = gtk_window_state_changed;
  widget_class->style_updated = gtk_window_style_updated;

  container_class->check_resize = gtk_window_check_resize;

  klass->set_focus = gtk_window_real_set_focus;

  klass->activate_default = gtk_window_real_activate_default;
  klass->activate_focus = gtk_window_real_activate_focus;
  klass->keys_changed = gtk_window_keys_changed;

  g_type_class_add_private (gobject_class, sizeof (GtkWindowPrivate));

  /* Construct */
  g_object_class_install_property (gobject_class,
                                   PROP_TYPE,
                                   g_param_spec_enum ("type",
						      P_("Window Type"),
						      P_("The type of the window"),
						      GTK_TYPE_WINDOW_TYPE,
						      GTK_WINDOW_TOPLEVEL,
						      GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  /* Regular Props */
  g_object_class_install_property (gobject_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        P_("Window Title"),
                                                        P_("The title of the window"),
                                                        NULL,
                                                        GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_ROLE,
                                   g_param_spec_string ("role",
							P_("Window Role"),
							P_("Unique identifier for the window to be used when restoring a session"),
							NULL,
							GTK_PARAM_READWRITE));

  /**
   * GtkWindow:startup-id:
   *
   * The :startup-id is a write-only property for setting window's
   * startup notification identifier. See gtk_window_set_startup_id()
   * for more details.
   *
   * Since: 2.12
   */
  g_object_class_install_property (gobject_class,
                                   PROP_STARTUP_ID,
                                   g_param_spec_string ("startup-id",
							P_("Startup ID"),
							P_("Unique startup identifier for the window used by startup-notification"),
							NULL,
							GTK_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_RESIZABLE,
                                   g_param_spec_boolean ("resizable",
							 P_("Resizable"),
							 P_("If TRUE, users can resize the window"),
							 TRUE,
							 GTK_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
                                   PROP_MODAL,
                                   g_param_spec_boolean ("modal",
							 P_("Modal"),
							 P_("If TRUE, the window is modal (other windows are not usable while this one is up)"),
							 FALSE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_WIN_POS,
                                   g_param_spec_enum ("window-position",
						      P_("Window Position"),
						      P_("The initial position of the window"),
						      GTK_TYPE_WINDOW_POSITION,
						      GTK_WIN_POS_NONE,
						      GTK_PARAM_READWRITE));
 
  g_object_class_install_property (gobject_class,
                                   PROP_DEFAULT_WIDTH,
                                   g_param_spec_int ("default-width",
						     P_("Default Width"),
						     P_("The default width of the window, used when initially showing the window"),
						     -1,
						     G_MAXINT,
						     -1,
						     GTK_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
                                   PROP_DEFAULT_HEIGHT,
                                   g_param_spec_int ("default-height",
						     P_("Default Height"),
						     P_("The default height of the window, used when initially showing the window"),
						     -1,
						     G_MAXINT,
						     -1,
						     GTK_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
                                   PROP_DESTROY_WITH_PARENT,
                                   g_param_spec_boolean ("destroy-with-parent",
							 P_("Destroy with Parent"),
							 P_("If this window should be destroyed when the parent is destroyed"),
                                                         FALSE,
							 GTK_PARAM_READWRITE));

  /**
   * GtkWindow:hide-titlebar-when-maximized:
   *
   * Whether the titlebar should be hidden during maximization.
   *
   * Since: 3.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_HIDE_TITLEBAR_WHEN_MAXIMIZED,
                                   g_param_spec_boolean ("hide-titlebar-when-maximized",
							 P_("Hide the titlebar during maximization"),
							 P_("If this window's titlebar should be hidden when the window is maximized"),
                                                         FALSE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_ICON,
                                   g_param_spec_object ("icon",
                                                        P_("Icon"),
                                                        P_("Icon for this window"),
                                                        GDK_TYPE_PIXBUF,
                                                        GTK_PARAM_READWRITE));

  /**
   * GtkWindow:mnemonics-visible:
   *
   * Whether mnemonics are currently visible in this window.
   *
   * This property is maintained by GTK+ based on the
   * #GtkSettings:gtk-auto-mnemonics setting and user input,
   * and should not be set by applications.
   *
   * Since: 2.20
   */
  g_object_class_install_property (gobject_class,
                                   PROP_MNEMONICS_VISIBLE,
                                   g_param_spec_boolean ("mnemonics-visible",
                                                         P_("Mnemonics Visible"),
                                                         P_("Whether mnemonics are currently visible in this window"),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));

  /**
   * GtkWindow:focus-visible:
   *
   * Whether 'focus rectangles' are currently visible in this window.
   *
   * This property is maintained by GTK+ based on the
   * #GtkSettings:gtk-visible-focus setting and user input
   * and should not be set by applications.
   *
   * Since: 2.20
   */
  g_object_class_install_property (gobject_class,
                                   PROP_FOCUS_VISIBLE,
                                   g_param_spec_boolean ("focus-visible",
                                                         P_("Focus Visible"),
                                                         P_("Whether focus rectangles are currently visible in this window"),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));
  
  /**
   * GtkWindow:icon-name:
   *
   * The :icon-name property specifies the name of the themed icon to
   * use as the window icon. See #GtkIconTheme for more details.
   *
   * Since: 2.6
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ICON_NAME,
                                   g_param_spec_string ("icon-name",
                                                        P_("Icon Name"),
                                                        P_("Name of the themed icon for this window"),
							NULL,
                                                        GTK_PARAM_READWRITE));
  
  g_object_class_install_property (gobject_class,
				   PROP_SCREEN,
				   g_param_spec_object ("screen",
 							P_("Screen"),
 							P_("The screen where this window will be displayed"),
							GDK_TYPE_SCREEN,
 							GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_IS_ACTIVE,
                                   g_param_spec_boolean ("is-active",
							 P_("Is Active"),
							 P_("Whether the toplevel is the current active window"),
							 FALSE,
							 GTK_PARAM_READABLE));
  
  g_object_class_install_property (gobject_class,
                                   PROP_HAS_TOPLEVEL_FOCUS,
                                   g_param_spec_boolean ("has-toplevel-focus",
							 P_("Focus in Toplevel"),
							 P_("Whether the input focus is within this GtkWindow"),
							 FALSE,
							 GTK_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
				   PROP_TYPE_HINT,
				   g_param_spec_enum ("type-hint",
                                                      P_("Type hint"),
                                                      P_("Hint to help the desktop environment understand what kind of window this is and how to treat it."),
                                                      GDK_TYPE_WINDOW_TYPE_HINT,
                                                      GDK_WINDOW_TYPE_HINT_NORMAL,
                                                      GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_SKIP_TASKBAR_HINT,
				   g_param_spec_boolean ("skip-taskbar-hint",
                                                         P_("Skip taskbar"),
                                                         P_("TRUE if the window should not be in the task bar."),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
				   PROP_SKIP_PAGER_HINT,
				   g_param_spec_boolean ("skip-pager-hint",
                                                         P_("Skip pager"),
                                                         P_("TRUE if the window should not be in the pager."),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));  

  g_object_class_install_property (gobject_class,
				   PROP_URGENCY_HINT,
				   g_param_spec_boolean ("urgency-hint",
                                                         P_("Urgent"),
                                                         P_("TRUE if the window should be brought to the user's attention."),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));  

  /**
   * GtkWindow:accept-focus:
   *
   * Whether the window should receive the input focus.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
				   PROP_ACCEPT_FOCUS,
				   g_param_spec_boolean ("accept-focus",
                                                         P_("Accept focus"),
                                                         P_("TRUE if the window should receive the input focus."),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));  

  /**
   * GtkWindow:focus-on-map:
   *
   * Whether the window should receive the input focus when mapped.
   *
   * Since: 2.6
   */
  g_object_class_install_property (gobject_class,
				   PROP_FOCUS_ON_MAP,
				   g_param_spec_boolean ("focus-on-map",
                                                         P_("Focus on map"),
                                                         P_("TRUE if the window should receive the input focus when mapped."),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));  

  /**
   * GtkWindow:decorated:
   *
   * Whether the window should be decorated by the window manager.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DECORATED,
                                   g_param_spec_boolean ("decorated",
							 P_("Decorated"),
							 P_("Whether the window should be decorated by the window manager"),
							 TRUE,
							 GTK_PARAM_READWRITE));

  /**
   * GtkWindow:deletable:
   *
   * Whether the window frame should have a close button.
   *
   * Since: 2.10
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DELETABLE,
                                   g_param_spec_boolean ("deletable",
							 P_("Deletable"),
							 P_("Whether the window frame should have a close button"),
							 TRUE,
							 GTK_PARAM_READWRITE));

  /**
   * GtkWindow:has-resize-grip
   *
   * Whether the window has a corner resize grip.
   *
   * Note that the resize grip is only shown if the window is
   * actually resizable and not maximized. Use
   * #GtkWindow:resize-grip-visible to find out if the resize
   * grip is currently shown.
   *
   * Since: 3.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_HAS_RESIZE_GRIP,
                                   g_param_spec_boolean ("has-resize-grip",
                                                         P_("Resize grip"),
                                                         P_("Specifies whether the window should have a resize grip"),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));

  /**
   * GtkWindow:resize-grip-visible:
   *
   * Whether a corner resize grip is currently shown.
   *
   * Since: 3.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_RESIZE_GRIP_VISIBLE,
                                   g_param_spec_boolean ("resize-grip-visible",
                                                         P_("Resize grip is visible"),
                                                         P_("Specifies whether the window's resize grip is visible."),
                                                         FALSE,
                                                         GTK_PARAM_READABLE));


  /**
   * GtkWindow:gravity:
   *
   * The window gravity of the window. See gtk_window_move() and #GdkGravity for
   * more details about window gravity.
   *
   * Since: 2.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_GRAVITY,
                                   g_param_spec_enum ("gravity",
						      P_("Gravity"),
						      P_("The window gravity of the window"),
						      GDK_TYPE_GRAVITY,
						      GDK_GRAVITY_NORTH_WEST,
						      GTK_PARAM_READWRITE));


  /**
   * GtkWindow:transient-for:
   *
   * The transient parent of the window. See gtk_window_set_transient_for() for
   * more details about transient windows.
   *
   * Since: 2.10
   */
  g_object_class_install_property (gobject_class,
				   PROP_TRANSIENT_FOR,
				   g_param_spec_object ("transient-for",
							P_("Transient for Window"),
							P_("The transient parent of the dialog"),
							GTK_TYPE_WINDOW,
							GTK_PARAM_READWRITE| G_PARAM_CONSTRUCT));

  /**
   * GtkWindow:attached-to:
   *
   * The widget to which this window is attached.
   * See gtk_window_set_attached_to().
   *
   * Examples of places where specifying this relation is useful are
   * for instance a #GtkMenu created by a #GtkComboBox, a completion
   * popup window created by #GtkEntry or a typeahead search entry
   * created by #GtkTreeView.
   *
   * Since: 3.4
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ATTACHED_TO,
                                   g_param_spec_object ("attached-to",
                                                        P_("Attached to Widget"),
                                                        P_("The widget where the window is attached"),
                                                        GTK_TYPE_WIDGET,
                                                        GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GtkWindow:opacity:
   *
   * The requested opacity of the window. See gtk_window_set_opacity() for
   * more details about window opacity.
   *
   * Since: 2.12
   */
  g_object_class_install_property (gobject_class,
				   PROP_OPACITY,
				   g_param_spec_double ("opacity",
							P_("Opacity for Window"),
							P_("The opacity of the window, from 0 to 1"),
							0.0,
							1.0,
							1.0,
							GTK_PARAM_READWRITE));

  /* Style properties.
   */
  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_int ("resize-grip-width",
                                                             P_("Width of resize grip"),
                                                             P_("Width of resize grip"),
                                                             0, G_MAXINT, 16, GTK_PARAM_READWRITE));

  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_int ("resize-grip-height",
                                                             P_("Height of resize grip"),
                                                             P_("Height of resize grip"),
                                                             0, G_MAXINT, 16, GTK_PARAM_READWRITE));


  /**
   * GtkWindow:application:
   *
   * The #GtkApplication associated with the window.
   *
   * The application will be kept alive for at least as long as it
   * has any windows associated with it (see g_application_hold()
   * for a way to keep it alive without windows).
   *
   * Normally, the connection between the application and the window
   * will remain until the window is destroyed, but you can explicitly
   * remove it by setting the ::application property to %NULL.
   *
   * Since: 3.0
   */
  g_object_class_install_property (gobject_class,
                                   PROP_APPLICATION,
                                   g_param_spec_object ("application",
                                                        P_("GtkApplication"),
                                                        P_("The GtkApplication for the window"),
                                                        GTK_TYPE_APPLICATION,
                                                        GTK_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  window_signals[SET_FOCUS] =
    g_signal_new (I_("set-focus"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtkWindowClass, set_focus),
                  NULL, NULL,
                  _gtk_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  GTK_TYPE_WIDGET);

  /**
   * GtkWindow::activate-focus:
   * @window: the window which received the signal
   *
   * The ::activate-focus signal is a
   * <link linkend="keybinding-signals">keybinding signal</link>
   * which gets emitted when the user activates the currently
   * focused widget of @window.
   */
  window_signals[ACTIVATE_FOCUS] =
    g_signal_new (I_("activate-focus"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkWindowClass, activate_focus),
                  NULL, NULL,
                  _gtk_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  /**
   * GtkWindow::activate-default:
   * @window: the window which received the signal
   *
   * The ::activate-default signal is a
   * <link linkend="keybinding-signals">keybinding signal</link>
   * which gets emitted when the user activates the default widget
   * of @window.
   */
  window_signals[ACTIVATE_DEFAULT] =
    g_signal_new (I_("activate-default"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkWindowClass, activate_default),
                  NULL, NULL,
                  _gtk_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  /**
   * GtkWindow::keys-changed:
   * @window: the window which received the signal
   *
   * The ::keys-changed signal gets emitted when the set of accelerators
   * or mnemonics that are associated with @window changes.
   */
  window_signals[KEYS_CHANGED] =
    g_signal_new (I_("keys-changed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_FIRST,
                  G_STRUCT_OFFSET (GtkWindowClass, keys_changed),
                  NULL, NULL,
                  _gtk_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  /*
   * Key bindings
   */

  binding_set = gtk_binding_set_by_class (klass);

  gtk_binding_entry_add_signal (binding_set, GDK_KEY_space, 0,
                                "activate-focus", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Space, 0,
                                "activate-focus", 0);
  
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_Return, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_ISO_Enter, 0,
                                "activate-default", 0);
  gtk_binding_entry_add_signal (binding_set, GDK_KEY_KP_Enter, 0,
                                "activate-default", 0);

  add_arrow_bindings (binding_set, GDK_KEY_Up, GTK_DIR_UP);
  add_arrow_bindings (binding_set, GDK_KEY_Down, GTK_DIR_DOWN);
  add_arrow_bindings (binding_set, GDK_KEY_Left, GTK_DIR_LEFT);
  add_arrow_bindings (binding_set, GDK_KEY_Right, GTK_DIR_RIGHT);

  add_tab_bindings (binding_set, 0, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);

  gtk_widget_class_set_accessible_type (widget_class, GTK_TYPE_WINDOW_ACCESSIBLE);
}

static void
gtk_window_init (GtkWindow *window)
{
  GtkStyleContext *context;
  GtkWindowPrivate *priv;

  window->priv = G_TYPE_INSTANCE_GET_PRIVATE (window,
                                              GTK_TYPE_WINDOW,
                                              GtkWindowPrivate);
  priv = window->priv;
  
  gtk_widget_set_has_window (GTK_WIDGET (window), TRUE);
  _gtk_widget_set_is_toplevel (GTK_WIDGET (window), TRUE);

  _gtk_widget_set_anchored (GTK_WIDGET (window), TRUE);

  gtk_container_set_resize_mode (GTK_CONTAINER (window), GTK_RESIZE_QUEUE);

  priv->title = NULL;
  priv->wmclass_name = g_strdup (g_get_prgname ());
  priv->wmclass_class = g_strdup (gdk_get_program_class ());
  priv->wm_role = NULL;
  priv->geometry_info = NULL;
  priv->type = GTK_WINDOW_TOPLEVEL;
  priv->focus_widget = NULL;
  priv->default_widget = NULL;
  priv->configure_request_count = 0;
  priv->resizable = TRUE;
  priv->configure_notify_received = FALSE;
  priv->position = GTK_WIN_POS_NONE;
  priv->need_default_size = TRUE;
  priv->need_default_position = TRUE;
  priv->modal = FALSE;
  priv->gdk_type_hint = GDK_WINDOW_TYPE_HINT_NORMAL;
  priv->gravity = GDK_GRAVITY_NORTH_WEST;
  priv->decorated = TRUE;
  priv->mnemonic_modifier = GDK_MOD1_MASK;
  priv->screen = gdk_screen_get_default ();

  priv->accept_focus = TRUE;
  priv->focus_on_map = TRUE;
  priv->deletable = TRUE;
  priv->type_hint = GDK_WINDOW_TYPE_HINT_NORMAL;
  priv->opacity = 1.0;
  priv->startup_id = NULL;
  priv->initial_timestamp = GDK_CURRENT_TIME;
  priv->has_resize_grip = TRUE;
  priv->mnemonics_visible = TRUE;
  priv->focus_visible = TRUE;

  g_object_ref_sink (window);
  priv->has_user_ref_count = TRUE;
  toplevel_list = g_slist_prepend (toplevel_list, window);

  if (priv->screen)
    g_signal_connect (priv->screen, "composited-changed",
                      G_CALLBACK (gtk_window_on_composited_changed), window);

#ifdef GDK_WINDOWING_X11
  g_signal_connect (gtk_settings_get_for_screen (priv->screen),
                    "notify::gtk-application-prefer-dark-theme",
                    G_CALLBACK (gtk_window_on_theme_variant_changed), window);
#endif

  context = gtk_widget_get_style_context (GTK_WIDGET (window));
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_BACKGROUND);
}

static void
gtk_window_set_property (GObject      *object,
			 guint         prop_id,
			 const GValue *value,
			 GParamSpec   *pspec)
{
  GtkWindow  *window = GTK_WINDOW (object);
  GtkWindowPrivate *priv = window->priv;

  switch (prop_id)
    {
    case PROP_TYPE:
      priv->type = g_value_get_enum (value);
      break;
    case PROP_TITLE:
      gtk_window_set_title (window, g_value_get_string (value));
      break;
    case PROP_ROLE:
      gtk_window_set_role (window, g_value_get_string (value));
      break;
    case PROP_STARTUP_ID:
      gtk_window_set_startup_id (window, g_value_get_string (value));
      break;
    case PROP_RESIZABLE:
      gtk_window_set_resizable (window, g_value_get_boolean (value));
      break;
    case PROP_MODAL:
      gtk_window_set_modal (window, g_value_get_boolean (value));
      break;
    case PROP_WIN_POS:
      gtk_window_set_position (window, g_value_get_enum (value));
      break;
    case PROP_DEFAULT_WIDTH:
      gtk_window_set_default_size_internal (window,
                                            TRUE, g_value_get_int (value),
                                            FALSE, -1, FALSE);
      break;
    case PROP_DEFAULT_HEIGHT:
      gtk_window_set_default_size_internal (window,
                                            FALSE, -1,
                                            TRUE, g_value_get_int (value), FALSE);
      break;
    case PROP_DESTROY_WITH_PARENT:
      gtk_window_set_destroy_with_parent (window, g_value_get_boolean (value));
      break;
    case PROP_HIDE_TITLEBAR_WHEN_MAXIMIZED:
      gtk_window_set_hide_titlebar_when_maximized (window, g_value_get_boolean (value));
      break;
    case PROP_ICON:
      gtk_window_set_icon (window,
                           g_value_get_object (value));
      break;
    case PROP_ICON_NAME:
      gtk_window_set_icon_name (window, g_value_get_string (value));
      break;
    case PROP_SCREEN:
      gtk_window_set_screen (window, g_value_get_object (value));
      break;
    case PROP_TYPE_HINT:
      gtk_window_set_type_hint (window,
                                g_value_get_enum (value));
      break;
    case PROP_SKIP_TASKBAR_HINT:
      gtk_window_set_skip_taskbar_hint (window,
                                        g_value_get_boolean (value));
      break;
    case PROP_SKIP_PAGER_HINT:
      gtk_window_set_skip_pager_hint (window,
                                      g_value_get_boolean (value));
      break;
    case PROP_URGENCY_HINT:
      gtk_window_set_urgency_hint (window,
				   g_value_get_boolean (value));
      break;
    case PROP_ACCEPT_FOCUS:
      gtk_window_set_accept_focus (window,
				   g_value_get_boolean (value));
      break;
    case PROP_FOCUS_ON_MAP:
      gtk_window_set_focus_on_map (window,
				   g_value_get_boolean (value));
      break;
    case PROP_DECORATED:
      gtk_window_set_decorated (window, g_value_get_boolean (value));
      break;
    case PROP_DELETABLE:
      gtk_window_set_deletable (window, g_value_get_boolean (value));
      break;
    case PROP_GRAVITY:
      gtk_window_set_gravity (window, g_value_get_enum (value));
      break;
    case PROP_TRANSIENT_FOR:
      gtk_window_set_transient_for (window, g_value_get_object (value));
      break;
    case PROP_ATTACHED_TO:
      gtk_window_set_attached_to (window, g_value_get_object (value));
      break;
    case PROP_OPACITY:
      gtk_window_set_opacity (window, g_value_get_double (value));
      break;
    case PROP_HAS_RESIZE_GRIP:
      gtk_window_set_has_resize_grip (window, g_value_get_boolean (value));
      break;
    case PROP_APPLICATION:
      gtk_window_set_application (window, g_value_get_object (value));
      break;
    case PROP_MNEMONICS_VISIBLE:
      gtk_window_set_mnemonics_visible (window, g_value_get_boolean (value));
      break;
    case PROP_FOCUS_VISIBLE:
      gtk_window_set_focus_visible (window, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_window_get_property (GObject      *object,
			 guint         prop_id,
			 GValue       *value,
			 GParamSpec   *pspec)
{
  GtkWindow  *window = GTK_WINDOW (object);
  GtkWindowPrivate *priv = window->priv;

  switch (prop_id)
    {
      GtkWindowGeometryInfo *info;
    case PROP_TYPE:
      g_value_set_enum (value, priv->type);
      break;
    case PROP_ROLE:
      g_value_set_string (value, priv->wm_role);
      break;
    case PROP_TITLE:
      g_value_set_string (value, priv->title);
      break;
    case PROP_RESIZABLE:
      g_value_set_boolean (value, priv->resizable);
      break;
    case PROP_MODAL:
      g_value_set_boolean (value, priv->modal);
      break;
    case PROP_WIN_POS:
      g_value_set_enum (value, priv->position);
      break;
    case PROP_DEFAULT_WIDTH:
      info = gtk_window_get_geometry_info (window, FALSE);
      if (!info)
	g_value_set_int (value, -1);
      else
	g_value_set_int (value, info->default_width);
      break;
    case PROP_DEFAULT_HEIGHT:
      info = gtk_window_get_geometry_info (window, FALSE);
      if (!info)
	g_value_set_int (value, -1);
      else
	g_value_set_int (value, info->default_height);
      break;
    case PROP_DESTROY_WITH_PARENT:
      g_value_set_boolean (value, priv->destroy_with_parent);
      break;
    case PROP_HIDE_TITLEBAR_WHEN_MAXIMIZED:
      g_value_set_boolean (value, priv->hide_titlebar_when_maximized);
      break;
    case PROP_ICON:
      g_value_set_object (value, gtk_window_get_icon (window));
      break;
    case PROP_ICON_NAME:
      g_value_set_string (value, gtk_window_get_icon_name (window));
      break;
    case PROP_SCREEN:
      g_value_set_object (value, priv->screen);
      break;
    case PROP_IS_ACTIVE:
      g_value_set_boolean (value, priv->is_active);
      break;
    case PROP_HAS_TOPLEVEL_FOCUS:
      g_value_set_boolean (value, priv->has_toplevel_focus);
      break;
    case PROP_TYPE_HINT:
      g_value_set_enum (value, priv->type_hint);
      break;
    case PROP_SKIP_TASKBAR_HINT:
      g_value_set_boolean (value,
                           gtk_window_get_skip_taskbar_hint (window));
      break;
    case PROP_SKIP_PAGER_HINT:
      g_value_set_boolean (value,
                           gtk_window_get_skip_pager_hint (window));
      break;
    case PROP_URGENCY_HINT:
      g_value_set_boolean (value,
                           gtk_window_get_urgency_hint (window));
      break;
    case PROP_ACCEPT_FOCUS:
      g_value_set_boolean (value,
                           gtk_window_get_accept_focus (window));
      break;
    case PROP_FOCUS_ON_MAP:
      g_value_set_boolean (value,
                           gtk_window_get_focus_on_map (window));
      break;
    case PROP_DECORATED:
      g_value_set_boolean (value, gtk_window_get_decorated (window));
      break;
    case PROP_DELETABLE:
      g_value_set_boolean (value, gtk_window_get_deletable (window));
      break;
    case PROP_GRAVITY:
      g_value_set_enum (value, gtk_window_get_gravity (window));
      break;
    case PROP_TRANSIENT_FOR:
      g_value_set_object (value, gtk_window_get_transient_for (window));
      break;
    case PROP_ATTACHED_TO:
      g_value_set_object (value, gtk_window_get_attached_to (window));
      break;
    case PROP_OPACITY:
      g_value_set_double (value, gtk_window_get_opacity (window));
      break;
    case PROP_HAS_RESIZE_GRIP:
      g_value_set_boolean (value, priv->has_resize_grip);
      break;
    case PROP_RESIZE_GRIP_VISIBLE:
      g_value_set_boolean (value, gtk_window_resize_grip_is_visible (window));
      break;
    case PROP_APPLICATION:
      g_value_set_object (value, gtk_window_get_application (window));
      break;
    case PROP_MNEMONICS_VISIBLE:
      g_value_set_boolean (value, priv->mnemonics_visible);
      break;
    case PROP_FOCUS_VISIBLE:
      g_value_set_boolean (value, priv->focus_visible);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_window_buildable_interface_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->set_buildable_property = gtk_window_buildable_set_buildable_property;
  iface->parser_finished = gtk_window_buildable_parser_finished;
  iface->custom_tag_start = gtk_window_buildable_custom_tag_start;
  iface->custom_finished = gtk_window_buildable_custom_finished;
}

static void
gtk_window_buildable_set_buildable_property (GtkBuildable        *buildable,
					     GtkBuilder          *builder,
					     const gchar         *name,
					     const GValue        *value)
{
  GtkWindow *window = GTK_WINDOW (buildable);
  GtkWindowPrivate *priv = window->priv;

  if (strcmp (name, "visible") == 0 && g_value_get_boolean (value))
    priv->builder_visible = TRUE;
  else
    parent_buildable_iface->set_buildable_property (buildable, builder, name, value);
}

static void
gtk_window_buildable_parser_finished (GtkBuildable *buildable,
				      GtkBuilder   *builder)
{
  GtkWindow *window = GTK_WINDOW (buildable);
  GtkWindowPrivate *priv = window->priv;
  GObject *object;
  GSList *accels, *l;

  if (priv->builder_visible)
    gtk_widget_show (GTK_WIDGET (buildable));

  accels = g_object_get_qdata (G_OBJECT (buildable), quark_gtk_buildable_accels);
  for (l = accels; l; l = l->next)
    {
      object = gtk_builder_get_object (builder, l->data);
      if (!object)
	{
	  g_warning ("Unknown accel group %s specified in window %s",
		     (const gchar*)l->data, gtk_buildable_get_name (buildable));
	  continue;
	}
      gtk_window_add_accel_group (GTK_WINDOW (buildable),
				  GTK_ACCEL_GROUP (object));
      g_free (l->data);
    }

  g_object_set_qdata (G_OBJECT (buildable), quark_gtk_buildable_accels, NULL);

  parent_buildable_iface->parser_finished (buildable, builder);
}

typedef struct {
  GObject *object;
  GSList *items;
} GSListSubParserData;

static void
window_start_element (GMarkupParseContext *context,
			  const gchar         *element_name,
			  const gchar        **names,
			  const gchar        **values,
			  gpointer            user_data,
			  GError            **error)
{
  guint i;
  GSListSubParserData *data = (GSListSubParserData*)user_data;

  if (strcmp (element_name, "group") == 0)
    {
      for (i = 0; names[i]; i++)
	{
	  if (strcmp (names[i], "name") == 0)
	    data->items = g_slist_prepend (data->items, g_strdup (values[i]));
	}
    }
  else if (strcmp (element_name, "accel-groups") == 0)
    return;
  else
    g_warning ("Unsupported tag type for GtkWindow: %s\n",
	       element_name);

}

static const GMarkupParser window_parser =
  {
    window_start_element
  };

static gboolean
gtk_window_buildable_custom_tag_start (GtkBuildable  *buildable,
				       GtkBuilder    *builder,
				       GObject       *child,
				       const gchar   *tagname,
				       GMarkupParser *parser,
				       gpointer      *data)
{
  GSListSubParserData *parser_data;

  if (parent_buildable_iface->custom_tag_start (buildable, builder, child, 
						tagname, parser, data))
    return TRUE;

  if (strcmp (tagname, "accel-groups") == 0)
    {
      parser_data = g_slice_new0 (GSListSubParserData);
      parser_data->items = NULL;
      parser_data->object = G_OBJECT (buildable);

      *parser = window_parser;
      *data = parser_data;
      return TRUE;
    }

  return FALSE;
}

static void
gtk_window_buildable_custom_finished (GtkBuildable  *buildable,
					  GtkBuilder    *builder,
					  GObject       *child,
					  const gchar   *tagname,
					  gpointer       user_data)
{
  GSListSubParserData *data;

  parent_buildable_iface->custom_finished (buildable, builder, child, 
					   tagname, user_data);

  if (strcmp (tagname, "accel-groups") != 0)
    return;
  
  data = (GSListSubParserData*)user_data;

  g_object_set_qdata_full (G_OBJECT (buildable), quark_gtk_buildable_accels, 
			   data->items, (GDestroyNotify) g_slist_free);

  g_slice_free (GSListSubParserData, data);
}

/**
 * gtk_window_new:
 * @type: type of window
 * 
 * Creates a new #GtkWindow, which is a toplevel window that can
 * contain other widgets. Nearly always, the type of the window should
 * be #GTK_WINDOW_TOPLEVEL. If you're implementing something like a
 * popup menu from scratch (which is a bad idea, just use #GtkMenu),
 * you might use #GTK_WINDOW_POPUP. #GTK_WINDOW_POPUP is not for
 * dialogs, though in some other toolkits dialogs are called "popups".
 * In GTK+, #GTK_WINDOW_POPUP means a pop-up menu or pop-up tooltip.
 * On X11, popup windows are not controlled by the <link
 * linkend="gtk-X11-arch">window manager</link>.
 *
 * If you simply want an undecorated window (no window borders), use
 * gtk_window_set_decorated(), don't use #GTK_WINDOW_POPUP.
 * 
 * Return value: a new #GtkWindow.
 **/
GtkWidget*
gtk_window_new (GtkWindowType type)
{
  GtkWindowPrivate *priv;
  GtkWindow *window;

  g_return_val_if_fail (type >= GTK_WINDOW_TOPLEVEL && type <= GTK_WINDOW_POPUP, NULL);

  window = g_object_new (GTK_TYPE_WINDOW, NULL);
  priv = window->priv;

  priv->type = type;

  return GTK_WIDGET (window);
}

/**
 * gtk_window_set_title:
 * @window: a #GtkWindow
 * @title: title of the window
 * 
 * Sets the title of the #GtkWindow. The title of a window will be
 * displayed in its title bar; on the X Window System, the title bar
 * is rendered by the <link linkend="gtk-X11-arch">window
 * manager</link>, so exactly how the title appears to users may vary
 * according to a user's exact configuration. The title should help a
 * user distinguish this window from other windows they may have
 * open. A good title might include the application name and current
 * document filename, for example.
 * 
 **/
void
gtk_window_set_title (GtkWindow   *window,
		      const gchar *title)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  char *new_title;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  new_title = g_strdup (title);
  g_free (priv->title);
  priv->title = new_title;

  if (gtk_widget_get_realized (widget))
    {
      gdk_window_set_title (gtk_widget_get_window (widget),
                            priv->title);
    }

  g_object_notify (G_OBJECT (window), "title");
}

/**
 * gtk_window_get_title:
 * @window: a #GtkWindow
 *
 * Retrieves the title of the window. See gtk_window_set_title().
 *
 * Return value: the title of the window, or %NULL if none has
 *    been set explicitely. The returned string is owned by the widget
 *    and must not be modified or freed.
 **/
const gchar *
gtk_window_get_title (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->title;
}

/**
 * gtk_window_set_wmclass:
 * @window: a #GtkWindow
 * @wmclass_name: window name hint
 * @wmclass_class: window class hint
 *
 * Don't use this function. It sets the X Window System "class" and
 * "name" hints for a window.  According to the ICCCM, you should
 * always set these to the same value for all windows in an
 * application, and GTK+ sets them to that value by default, so calling
 * this function is sort of pointless. However, you may want to call
 * gtk_window_set_role() on each window in your application, for the
 * benefit of the session manager. Setting the role allows the window
 * manager to restore window positions when loading a saved session.
 * 
 **/
void
gtk_window_set_wmclass (GtkWindow *window,
			const gchar *wmclass_name,
			const gchar *wmclass_class)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  g_free (priv->wmclass_name);
  priv->wmclass_name = g_strdup (wmclass_name);

  g_free (priv->wmclass_class);
  priv->wmclass_class = g_strdup (wmclass_class);

  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    g_warning ("gtk_window_set_wmclass: shouldn't set wmclass after window is realized!\n");
}

/**
 * gtk_window_set_role:
 * @window: a #GtkWindow
 * @role: unique identifier for the window to be used when restoring a session
 *
 * This function is only useful on X11, not with other GTK+ targets.
 * 
 * In combination with the window title, the window role allows a
 * <link linkend="gtk-X11-arch">window manager</link> to identify "the
 * same" window when an application is restarted. So for example you
 * might set the "toolbox" role on your app's toolbox window, so that
 * when the user restarts their session, the window manager can put
 * the toolbox back in the same place.
 *
 * If a window already has a unique title, you don't need to set the
 * role, since the WM can use the title to identify the window when
 * restoring the session.
 * 
 **/
void
gtk_window_set_role (GtkWindow   *window,
                     const gchar *role)
{
  GtkWindowPrivate *priv;
  char *new_role;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  new_role = g_strdup (role);
  g_free (priv->wm_role);
  priv->wm_role = new_role;

  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    gdk_window_set_role (gtk_widget_get_window (GTK_WIDGET (window)),
                         priv->wm_role);

  g_object_notify (G_OBJECT (window), "role");
}

/**
 * gtk_window_set_startup_id:
 * @window: a #GtkWindow
 * @startup_id: a string with startup-notification identifier
 *
 * Startup notification identifiers are used by desktop environment to 
 * track application startup, to provide user feedback and other 
 * features. This function changes the corresponding property on the
 * underlying GdkWindow. Normally, startup identifier is managed 
 * automatically and you should only use this function in special cases
 * like transferring focus from other processes. You should use this
 * function before calling gtk_window_present() or any equivalent
 * function generating a window map event.
 *
 * This function is only useful on X11, not with other GTK+ targets.
 * 
 * Since: 2.12
 **/
void
gtk_window_set_startup_id (GtkWindow   *window,
                           const gchar *startup_id)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  g_free (priv->startup_id);
  priv->startup_id = g_strdup (startup_id);

  if (gtk_widget_get_realized (widget))
    {
      GdkWindow *gdk_window;
      guint32 timestamp = extract_time_from_startup_id (priv->startup_id);

      gdk_window = gtk_widget_get_window (widget);

#ifdef GDK_WINDOWING_X11
      if (timestamp != GDK_CURRENT_TIME && GDK_IS_X11_WINDOW(gdk_window))
	gdk_x11_window_set_user_time (gdk_window, timestamp);
#endif

      /* Here we differentiate real and "fake" startup notification IDs,
       * constructed on purpose just to pass interaction timestamp
       */
      if (startup_id_is_fake (priv->startup_id))
	gtk_window_present_with_time (window, timestamp);
      else 
        {
          gdk_window_set_startup_id (gdk_window,
                                     priv->startup_id);
          
          /* If window is mapped, terminate the startup-notification too */
          if (gtk_widget_get_mapped (widget) &&
              !disable_startup_notification)
            gdk_notify_startup_complete_with_id (priv->startup_id);
        }
    }

  g_object_notify (G_OBJECT (window), "startup-id");
}

/**
 * gtk_window_get_role:
 * @window: a #GtkWindow
 *
 * Returns the role of the window. See gtk_window_set_role() for
 * further explanation.
 *
 * Return value: the role of the window if set, or %NULL. The
 *   returned is owned by the widget and must not be modified
 *   or freed.
 **/
const gchar *
gtk_window_get_role (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->wm_role;
}

/**
 * gtk_window_set_focus:
 * @window: a #GtkWindow
 * @focus: (allow-none): widget to be the new focus widget, or %NULL to unset
 *   any focus widget for the toplevel window.
 *
 * If @focus is not the current focus widget, and is focusable, sets
 * it as the focus widget for the window. If @focus is %NULL, unsets
 * the focus widget for this window. To set the focus to a particular
 * widget in the toplevel, it is usually more convenient to use
 * gtk_widget_grab_focus() instead of this function.
 **/
void
gtk_window_set_focus (GtkWindow *window,
		      GtkWidget *focus)
{
  GtkWindowPrivate *priv;
  GtkWidget *parent;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (focus)
    {
      g_return_if_fail (GTK_IS_WIDGET (focus));
      g_return_if_fail (gtk_widget_get_can_focus (focus));
    }

  if (focus)
    gtk_widget_grab_focus (focus);
  else
    {
      /* Clear the existing focus chain, so that when we focus into
       * the window again, we start at the beginnning.
       */
      GtkWidget *widget = priv->focus_widget;
      if (widget)
	{
	  while ((parent = gtk_widget_get_parent (widget)))
	    {
	      widget = parent;
	      gtk_container_set_focus_child (GTK_CONTAINER (widget), NULL);
	    }
	}
      
      _gtk_window_internal_set_focus (window, NULL);
    }
}

void
_gtk_window_internal_set_focus (GtkWindow *window,
				GtkWidget *focus)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if ((priv->focus_widget != focus) ||
      (focus && !gtk_widget_has_focus (focus)))
    g_signal_emit (window, window_signals[SET_FOCUS], 0, focus);
}

/**
 * gtk_window_set_default:
 * @window: a #GtkWindow
 * @default_widget: (allow-none): widget to be the default, or %NULL to unset the
 *                  default widget for the toplevel.
 *
 * The default widget is the widget that's activated when the user
 * presses Enter in a dialog (for example). This function sets or
 * unsets the default widget for a #GtkWindow about. When setting
 * (rather than unsetting) the default widget it's generally easier to
 * call gtk_widget_grab_focus() on the widget. Before making a widget
 * the default widget, you must set the #GTK_CAN_DEFAULT flag on the
 * widget you'd like to make the default using GTK_WIDGET_SET_FLAGS().
 **/
void
gtk_window_set_default (GtkWindow *window,
			GtkWidget *default_widget)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (default_widget)
    g_return_if_fail (gtk_widget_get_can_default (default_widget));

  if (priv->default_widget != default_widget)
    {
      GtkWidget *old_default_widget = NULL;
      
      if (default_widget)
	g_object_ref (default_widget);

      if (priv->default_widget)
	{
          old_default_widget = priv->default_widget;

          if (priv->focus_widget != priv->default_widget ||
              !gtk_widget_get_receives_default (priv->default_widget))
            _gtk_widget_set_has_default (priv->default_widget, FALSE);

          gtk_widget_queue_draw (priv->default_widget);
	}

      priv->default_widget = default_widget;

      if (priv->default_widget)
	{
          if (priv->focus_widget == NULL ||
              !gtk_widget_get_receives_default (priv->focus_widget))
            _gtk_widget_set_has_default (priv->default_widget, TRUE);

          gtk_widget_queue_draw (priv->default_widget);
	}

      if (old_default_widget)
	g_object_notify (G_OBJECT (old_default_widget), "has-default");
      
      if (default_widget)
	{
	  g_object_notify (G_OBJECT (default_widget), "has-default");
	  g_object_unref (default_widget);
	}
    }
}

/**
 * gtk_window_get_default_widget:
 * @window: a #GtkWindow
 *
 * Returns the default widget for @window. See gtk_window_set_default()
 * for more details.
 *
 * Returns: (transfer none): the default widget, or %NULL if there is none.
 *
 * Since: 2.14
 **/
GtkWidget *
gtk_window_get_default_widget (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->default_widget;
}

static gboolean
handle_keys_changed (gpointer data)
{
  GtkWindow *window = GTK_WINDOW (data);
  GtkWindowPrivate *priv = window->priv;

  if (priv->keys_changed_handler)
    {
      g_source_remove (priv->keys_changed_handler);
      priv->keys_changed_handler = 0;
    }

  g_signal_emit (window, window_signals[KEYS_CHANGED], 0);
  
  return FALSE;
}

static void
gtk_window_notify_keys_changed (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (!priv->keys_changed_handler)
    priv->keys_changed_handler = gdk_threads_add_idle (handle_keys_changed, window);
}

/**
 * gtk_window_add_accel_group:
 * @window: window to attach accelerator group to
 * @accel_group: a #GtkAccelGroup
 *
 * Associate @accel_group with @window, such that calling
 * gtk_accel_groups_activate() on @window will activate accelerators
 * in @accel_group.
 **/
void
gtk_window_add_accel_group (GtkWindow     *window,
			    GtkAccelGroup *accel_group)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_ACCEL_GROUP (accel_group));

  _gtk_accel_group_attach (accel_group, G_OBJECT (window));
  g_signal_connect_object (accel_group, "accel-changed",
			   G_CALLBACK (gtk_window_notify_keys_changed),
			   window, G_CONNECT_SWAPPED);
  gtk_window_notify_keys_changed (window);
}

/**
 * gtk_window_remove_accel_group:
 * @window: a #GtkWindow
 * @accel_group: a #GtkAccelGroup
 *
 * Reverses the effects of gtk_window_add_accel_group().
 **/
void
gtk_window_remove_accel_group (GtkWindow     *window,
			       GtkAccelGroup *accel_group)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_ACCEL_GROUP (accel_group));

  g_signal_handlers_disconnect_by_func (accel_group,
					gtk_window_notify_keys_changed,
					window);
  _gtk_accel_group_detach (accel_group, G_OBJECT (window));
  gtk_window_notify_keys_changed (window);
}

static GtkMnemonicHash *
gtk_window_get_mnemonic_hash (GtkWindow *window,
			      gboolean   create)
{
  GtkWindowPrivate *private = window->priv;

  if (!private->mnemonic_hash && create)
    private->mnemonic_hash = _gtk_mnemonic_hash_new ();

  return private->mnemonic_hash;
}

/**
 * gtk_window_add_mnemonic:
 * @window: a #GtkWindow
 * @keyval: the mnemonic
 * @target: the widget that gets activated by the mnemonic
 *
 * Adds a mnemonic to this window.
 */
void
gtk_window_add_mnemonic (GtkWindow *window,
			 guint      keyval,
			 GtkWidget *target)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_WIDGET (target));

  _gtk_mnemonic_hash_add (gtk_window_get_mnemonic_hash (window, TRUE),
			  keyval, target);
  gtk_window_notify_keys_changed (window);
}

/**
 * gtk_window_remove_mnemonic:
 * @window: a #GtkWindow
 * @keyval: the mnemonic
 * @target: the widget that gets activated by the mnemonic
 *
 * Removes a mnemonic from this window.
 */
void
gtk_window_remove_mnemonic (GtkWindow *window,
			    guint      keyval,
			    GtkWidget *target)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_IS_WIDGET (target));
  
  _gtk_mnemonic_hash_remove (gtk_window_get_mnemonic_hash (window, TRUE),
			     keyval, target);
  gtk_window_notify_keys_changed (window);
}

/**
 * gtk_window_mnemonic_activate:
 * @window: a #GtkWindow
 * @keyval: the mnemonic
 * @modifier: the modifiers
 *
 * Activates the targets associated with the mnemonic.
 *
 * Returns: %TRUE if the activation is done.
 */
gboolean
gtk_window_mnemonic_activate (GtkWindow      *window,
			      guint           keyval,
			      GdkModifierType modifier)
{
  GtkWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;

  if (priv->mnemonic_modifier == (modifier & gtk_accelerator_get_default_mod_mask ()))
      {
	GtkMnemonicHash *mnemonic_hash = gtk_window_get_mnemonic_hash (window, FALSE);
	if (mnemonic_hash)
	  return _gtk_mnemonic_hash_activate (mnemonic_hash, keyval);
      }

  return FALSE;
}

/**
 * gtk_window_set_mnemonic_modifier:
 * @window: a #GtkWindow
 * @modifier: the modifier mask used to activate
 *               mnemonics on this window.
 *
 * Sets the mnemonic modifier for this window. 
 **/
void
gtk_window_set_mnemonic_modifier (GtkWindow      *window,
				  GdkModifierType modifier)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail ((modifier & ~GDK_MODIFIER_MASK) == 0);

  priv = window->priv;

  priv->mnemonic_modifier = modifier;
  gtk_window_notify_keys_changed (window);
}

/**
 * gtk_window_get_mnemonic_modifier:
 * @window: a #GtkWindow
 *
 * Returns the mnemonic modifier for this window. See
 * gtk_window_set_mnemonic_modifier().
 *
 * Return value: the modifier mask used to activate
 *               mnemonics on this window.
 **/
GdkModifierType
gtk_window_get_mnemonic_modifier (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), 0);

  return window->priv->mnemonic_modifier;
}

/**
 * gtk_window_set_position:
 * @window: a #GtkWindow.
 * @position: a position constraint.
 *
 * Sets a position constraint for this window. If the old or new
 * constraint is %GTK_WIN_POS_CENTER_ALWAYS, this will also cause
 * the window to be repositioned to satisfy the new constraint. 
 **/
void
gtk_window_set_position (GtkWindow         *window,
			 GtkWindowPosition  position)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (position == GTK_WIN_POS_CENTER_ALWAYS ||
      priv->position == GTK_WIN_POS_CENTER_ALWAYS)
    {
      GtkWindowGeometryInfo *info;

      info = gtk_window_get_geometry_info (window, TRUE);

      /* this flag causes us to re-request the CENTER_ALWAYS
       * constraint in gtk_window_move_resize(), see
       * comment in that function.
       */
      info->position_constraints_changed = TRUE;

      gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));
    }

  priv->position = position;
  
  g_object_notify (G_OBJECT (window), "window-position");
}

/**
 * gtk_window_activate_focus:
 * @window: a #GtkWindow
 * 
 * Activates the current focused widget within the window.
 * 
 * Return value: %TRUE if a widget got activated.
 **/
gboolean 
gtk_window_activate_focus (GtkWindow *window)
{
  GtkWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;

  if (priv->focus_widget && gtk_widget_is_sensitive (priv->focus_widget))
    return gtk_widget_activate (priv->focus_widget);

  return FALSE;
}

/**
 * gtk_window_get_focus:
 * @window: a #GtkWindow
 * 
 * Retrieves the current focused widget within the window.
 * Note that this is the widget that would have the focus
 * if the toplevel window focused; if the toplevel window
 * is not focused then  <literal>gtk_widget_has_focus (widget)</literal> will
 * not be %TRUE for the widget.
 *
 * Return value: (transfer none): the currently focused widget, or %NULL if there is none.
 **/
GtkWidget *
gtk_window_get_focus (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->focus_widget;
}

/**
 * gtk_window_activate_default:
 * @window: a #GtkWindow
 * 
 * Activates the default widget for the window, unless the current 
 * focused widget has been configured to receive the default action 
 * (see gtk_widget_set_receives_default()), in which case the
 * focused widget is activated. 
 * 
 * Return value: %TRUE if a widget got activated.
 **/
gboolean
gtk_window_activate_default (GtkWindow *window)
{
  GtkWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;

  if (priv->default_widget && gtk_widget_is_sensitive (priv->default_widget) &&
      (!priv->focus_widget || !gtk_widget_get_receives_default (priv->focus_widget)))
    return gtk_widget_activate (priv->default_widget);
  else if (priv->focus_widget && gtk_widget_is_sensitive (priv->focus_widget))
    return gtk_widget_activate (priv->focus_widget);

  return FALSE;
}

/**
 * gtk_window_set_modal:
 * @window: a #GtkWindow
 * @modal: whether the window is modal
 * 
 * Sets a window modal or non-modal. Modal windows prevent interaction
 * with other windows in the same application. To keep modal dialogs
 * on top of main application windows, use
 * gtk_window_set_transient_for() to make the dialog transient for the
 * parent; most <link linkend="gtk-X11-arch">window managers</link>
 * will then disallow lowering the dialog below the parent.
 * 
 * 
 **/
void
gtk_window_set_modal (GtkWindow *window,
		      gboolean   modal)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  modal = modal != FALSE;
  if (priv->modal == modal)
    return;

  priv->modal = modal;
  widget = GTK_WIDGET (window);
  
  /* adjust desired modality state */
  if (gtk_widget_get_realized (widget))
    {
      if (priv->modal)
	gdk_window_set_modal_hint (gtk_widget_get_window (widget), TRUE);
      else
	gdk_window_set_modal_hint (gtk_widget_get_window (widget), FALSE);
    }

  if (gtk_widget_get_visible (widget))
    {
      if (priv->modal)
	gtk_grab_add (widget);
      else
	gtk_grab_remove (widget);
    }

  g_object_notify (G_OBJECT (window), "modal");
}

/**
 * gtk_window_get_modal:
 * @window: a #GtkWindow
 * 
 * Returns whether the window is modal. See gtk_window_set_modal().
 *
 * Return value: %TRUE if the window is set to be modal and
 *               establishes a grab when shown
 **/
gboolean
gtk_window_get_modal (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->modal;
}

/**
 * gtk_window_list_toplevels:
 * 
 * Returns a list of all existing toplevel windows. The widgets
 * in the list are not individually referenced. If you want
 * to iterate through the list and perform actions involving
 * callbacks that might destroy the widgets, you <emphasis>must</emphasis> call
 * <literal>g_list_foreach (result, (GFunc)g_object_ref, NULL)</literal> first, and
 * then unref all the widgets afterwards.
 *
 * Return value: (element-type GtkWidget) (transfer container): list of toplevel widgets
 **/
GList*
gtk_window_list_toplevels (void)
{
  GList *list = NULL;
  GSList *slist;

  for (slist = toplevel_list; slist; slist = slist->next)
    list = g_list_prepend (list, slist->data);

  return list;
}

static void
remove_attach_widget (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (priv->attach_widget)
    {
      _gtk_widget_remove_attached_window (priv->attach_widget, window);

      g_object_unref (priv->attach_widget);
      priv->attach_widget = NULL;
    }
}

static void
gtk_window_dispose (GObject *object)
{
  GtkWindow *window = GTK_WINDOW (object);

  gtk_window_set_focus (window, NULL);
  gtk_window_set_default (window, NULL);

  remove_attach_widget (GTK_WINDOW (object));

  G_OBJECT_CLASS (gtk_window_parent_class)->dispose (object);
}

static void
parent_destroyed_callback (GtkWindow *parent, GtkWindow *child)
{
  gtk_widget_destroy (GTK_WIDGET (child));
}

static void
connect_parent_destroyed (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (priv->transient_parent)
    {
      g_signal_connect (priv->transient_parent,
                        "destroy",
                        G_CALLBACK (parent_destroyed_callback),
                        window);
    }  
}

static void
disconnect_parent_destroyed (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (priv->transient_parent)
    {
      g_signal_handlers_disconnect_by_func (priv->transient_parent,
					    parent_destroyed_callback,
					    window);
    }
}

static void
gtk_window_transient_parent_realized (GtkWidget *parent,
				      GtkWidget *window)
{
  if (gtk_widget_get_realized (window))
    gdk_window_set_transient_for (gtk_widget_get_window (window),
                                  gtk_widget_get_window (parent));
}

static void
gtk_window_transient_parent_unrealized (GtkWidget *parent,
					GtkWidget *window)
{
  if (gtk_widget_get_realized (window))
    gdk_property_delete (gtk_widget_get_window (window),
			 gdk_atom_intern_static_string ("WM_TRANSIENT_FOR"));
}

static void
gtk_window_transient_parent_screen_changed (GtkWindow	*parent,
					    GParamSpec	*pspec,
					    GtkWindow   *window)
{
  gtk_window_set_screen (window, parent->priv->screen);
}

static void       
gtk_window_unset_transient_for  (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (priv->transient_parent)
    {
      g_signal_handlers_disconnect_by_func (priv->transient_parent,
					    gtk_window_transient_parent_realized,
					    window);
      g_signal_handlers_disconnect_by_func (priv->transient_parent,
					    gtk_window_transient_parent_unrealized,
					    window);
      g_signal_handlers_disconnect_by_func (priv->transient_parent,
					    gtk_window_transient_parent_screen_changed,
					    window);
      g_signal_handlers_disconnect_by_func (priv->transient_parent,
					    gtk_widget_destroyed,
					    &priv->transient_parent);

      if (priv->destroy_with_parent)
        disconnect_parent_destroyed (window);

      priv->transient_parent = NULL;

      if (priv->transient_parent_group)
	{
	  priv->transient_parent_group = FALSE;
	  gtk_window_group_remove_window (priv->group,
					  window);
	}
    }
}

/**
 * gtk_window_set_transient_for:
 * @window: a #GtkWindow
 * @parent: (allow-none): parent window, or %NULL
 *
 * Dialog windows should be set transient for the main application
 * window they were spawned from. This allows <link
 * linkend="gtk-X11-arch">window managers</link> to e.g. keep the
 * dialog on top of the main window, or center the dialog over the
 * main window. gtk_dialog_new_with_buttons() and other convenience
 * functions in GTK+ will sometimes call
 * gtk_window_set_transient_for() on your behalf.
 *
 * Passing %NULL for @parent unsets the current transient window.
 *
 * On Windows, this function puts the child window on top of the parent,
 * much as the window manager would have done on X.
 */
void
gtk_window_set_transient_for  (GtkWindow *window,
			       GtkWindow *parent)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (parent == NULL || GTK_IS_WINDOW (parent));
  g_return_if_fail (window != parent);

  priv = window->priv;

  if (priv->transient_parent)
    {
      if (gtk_widget_get_realized (GTK_WIDGET (window)) &&
          gtk_widget_get_realized (GTK_WIDGET (priv->transient_parent)) &&
          (!parent || !gtk_widget_get_realized (GTK_WIDGET (parent))))
	gtk_window_transient_parent_unrealized (GTK_WIDGET (priv->transient_parent),
						GTK_WIDGET (window));

      gtk_window_unset_transient_for (window);
    }

  priv->transient_parent = parent;

  if (parent)
    {
      g_signal_connect (parent, "destroy",
			G_CALLBACK (gtk_widget_destroyed),
			&priv->transient_parent);
      g_signal_connect (parent, "realize",
			G_CALLBACK (gtk_window_transient_parent_realized),
			window);
      g_signal_connect (parent, "unrealize",
			G_CALLBACK (gtk_window_transient_parent_unrealized),
			window);
      g_signal_connect (parent, "notify::screen",
			G_CALLBACK (gtk_window_transient_parent_screen_changed),
			window);

      gtk_window_set_screen (window, parent->priv->screen);

      if (priv->destroy_with_parent)
        connect_parent_destroyed (window);
      
      if (gtk_widget_get_realized (GTK_WIDGET (window)) &&
	  gtk_widget_get_realized (GTK_WIDGET (parent)))
	gtk_window_transient_parent_realized (GTK_WIDGET (parent),
					      GTK_WIDGET (window));

      if (parent->priv->group)
	{
	  gtk_window_group_add_window (parent->priv->group, window);
	  priv->transient_parent_group = TRUE;
	}
    }
}

/**
 * gtk_window_get_transient_for:
 * @window: a #GtkWindow
 *
 * Fetches the transient parent for this window. See
 * gtk_window_set_transient_for().
 *
 * Return value: (transfer none): the transient parent for this window, or %NULL
 *    if no transient parent has been set.
 **/
GtkWindow *
gtk_window_get_transient_for (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->transient_parent;
}

/**
 * gtk_window_set_attached_to:
 * @window: a #GtkWindow
 * @attach_widget: (allow-none): a #GtkWidget, or %NULL
 *
 * Marks @window as attached to @attach_widget. This creates a logical binding
 * between the window and the widget it belongs to, which is used by GTK+ to
 * propagate information such as styling or accessibility to @window as if it
 * was a children of @attach_widget.
 *
 * Examples of places where specifying this relation is useful are for instance
 * a #GtkMenu created by a #GtkComboBox, a completion popup window
 * created by #GtkEntry or a typeahead search entry created by #GtkTreeView.
 *
 * Note that this function should not be confused with
 * gtk_window_set_transient_for(), which specifies a window manager relation
 * between two toplevels instead.
 *
 * Passing %NULL for @attach_widget detaches the window.
 *
 * Since: 3.4
 **/
void
gtk_window_set_attached_to (GtkWindow *window,
                            GtkWidget *attach_widget)
{
  GtkStyleContext *context;
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GTK_WIDGET (window) != attach_widget);

  priv = window->priv;

  if (priv->attach_widget == attach_widget)
    return;

  remove_attach_widget (window);

  priv->attach_widget = attach_widget;

  if (priv->attach_widget)
    {
      _gtk_widget_add_attached_window (priv->attach_widget, window);

      g_object_ref (priv->attach_widget);
    }

  /* Update the style, as the widget path might change. */
  context = gtk_widget_get_style_context (GTK_WIDGET (window));
  if (priv->attach_widget)
    gtk_style_context_set_parent (context, gtk_widget_get_style_context (priv->attach_widget));
  else
    gtk_style_context_set_parent (context, NULL);
}

/**
 * gtk_window_get_attached_to:
 * @window: a #GtkWindow
 *
 * Fetches the attach widget for this window. See
 * gtk_window_set_attached_to().
 *
 * Return value: (transfer none): the widget where the window is attached,
 *   or %NULL if the window is not attached to any widget.
 *
 * Since: 3.4
 **/
GtkWidget *
gtk_window_get_attached_to (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->attach_widget;
}

/**
 * gtk_window_set_opacity:
 * @window: a #GtkWindow
 * @opacity: desired opacity, between 0 and 1
 *
 * Request the windowing system to make @window partially transparent,
 * with opacity 0 being fully transparent and 1 fully opaque. (Values
 * of the opacity parameter are clamped to the [0,1] range.) On X11
 * this has any effect only on X screens with a compositing manager
 * running. See gtk_widget_is_composited(). On Windows it should work
 * always.
 * 
 * Note that setting a window's opacity after the window has been
 * shown causes it to flicker once on Windows.
 *
 * Since: 2.12
 **/
void       
gtk_window_set_opacity  (GtkWindow *window, 
			 gdouble    opacity)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (opacity < 0.0)
    opacity = 0.0;
  else if (opacity > 1.0)
    opacity = 1.0;

  priv->opacity_set = TRUE;
  priv->opacity = opacity;

  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    gdk_window_set_opacity (gtk_widget_get_window (GTK_WIDGET (window)),
                            priv->opacity);
}

/**
 * gtk_window_get_opacity:
 * @window: a #GtkWindow
 *
 * Fetches the requested opacity for this window. See
 * gtk_window_set_opacity().
 *
 * Return value: the requested opacity for this window.
 *
 * Since: 2.12
 **/
gdouble
gtk_window_get_opacity (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), 0.0);

  return window->priv->opacity;
}

/**
 * gtk_window_get_application:
 * @window: a #GtkWindow
 *
 * Gets the #GtkApplication associated with the window (if any).
 *
 * Return value: (transfer none): a #GtkApplication, or %NULL
 *
 * Since: 3.0
 **/
GtkApplication *
gtk_window_get_application (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->application;
}

static void
gtk_window_release_application (GtkWindow *window)
{
  if (window->priv->application)
    {
      GtkApplication *application;

      /* steal reference into temp variable */
      application = window->priv->application;
      window->priv->application = NULL;

      gtk_application_remove_window (application, window);
      g_object_unref (application);
    }
}

/**
 * gtk_window_set_application:
 * @window: a #GtkWindow
 * @application: (allow-none): a #GtkApplication, or %NULL
 *
 * Sets or unsets the #GtkApplication associated with the window.
 *
 * The application will be kept alive for at least as long as the window
 * is open.
 *
 * Since: 3.0
 **/
void
gtk_window_set_application (GtkWindow      *window,
                            GtkApplication *application)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  if (priv->application != application)
    {
      gtk_window_release_application (window);

      priv->application = application;

      if (priv->application != NULL)
        {
          g_object_ref (priv->application);

          gtk_application_add_window (priv->application, window);
        }

      /* don't use a normal cast: application may be NULL */
      gtk_widget_insert_action_group (GTK_WIDGET (window), "app", (GActionGroup *) application);

      g_object_notify (G_OBJECT (window), "application");
    }
}

/**
 * gtk_window_set_type_hint:
 * @window: a #GtkWindow
 * @hint: the window type
 *
 * By setting the type hint for the window, you allow the window
 * manager to decorate and handle the window in a way which is
 * suitable to the function of the window in your application.
 *
 * This function should be called before the window becomes visible.
 *
 * gtk_dialog_new_with_buttons() and other convenience functions in GTK+
 * will sometimes call gtk_window_set_type_hint() on your behalf.
 * 
 **/
void
gtk_window_set_type_hint (GtkWindow           *window, 
			  GdkWindowTypeHint    hint)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (!gtk_widget_get_mapped (GTK_WIDGET (window)));

  priv = window->priv;

  if (hint < GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU)
    priv->type_hint = hint;
  else
    priv->type_hint = GDK_WINDOW_TYPE_HINT_NORMAL;

  priv->reset_type_hint = TRUE;
  priv->gdk_type_hint = hint;
}

/**
 * gtk_window_get_type_hint:
 * @window: a #GtkWindow
 *
 * Gets the type hint for this window. See gtk_window_set_type_hint().
 *
 * Return value: the type hint for @window.
 **/
GdkWindowTypeHint
gtk_window_get_type_hint (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), GDK_WINDOW_TYPE_HINT_NORMAL);

  return window->priv->gdk_type_hint;
}

/**
 * gtk_window_set_skip_taskbar_hint:
 * @window: a #GtkWindow 
 * @setting: %TRUE to keep this window from appearing in the task bar
 * 
 * Windows may set a hint asking the desktop environment not to display
 * the window in the task bar. This function sets this hint.
 * 
 * Since: 2.2
 **/
void
gtk_window_set_skip_taskbar_hint (GtkWindow *window,
                                  gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->skips_taskbar != setting)
    {
      priv->skips_taskbar = setting;
      if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gdk_window_set_skip_taskbar_hint (gtk_widget_get_window (GTK_WIDGET (window)),
                                          priv->skips_taskbar);
      g_object_notify (G_OBJECT (window), "skip-taskbar-hint");
    }
}

/**
 * gtk_window_get_skip_taskbar_hint:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_skip_taskbar_hint()
 * 
 * Return value: %TRUE if window shouldn't be in taskbar
 * 
 * Since: 2.2
 **/
gboolean
gtk_window_get_skip_taskbar_hint (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->skips_taskbar;
}

/**
 * gtk_window_set_skip_pager_hint:
 * @window: a #GtkWindow 
 * @setting: %TRUE to keep this window from appearing in the pager
 * 
 * Windows may set a hint asking the desktop environment not to display
 * the window in the pager. This function sets this hint.
 * (A "pager" is any desktop navigation tool such as a workspace
 * switcher that displays a thumbnail representation of the windows
 * on the screen.)
 * 
 * Since: 2.2
 **/
void
gtk_window_set_skip_pager_hint (GtkWindow *window,
                                gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->skips_pager != setting)
    {
      priv->skips_pager = setting;
      if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gdk_window_set_skip_pager_hint (gtk_widget_get_window (GTK_WIDGET (window)),
                                        priv->skips_pager);
      g_object_notify (G_OBJECT (window), "skip-pager-hint");
    }
}

/**
 * gtk_window_get_skip_pager_hint:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_skip_pager_hint().
 * 
 * Return value: %TRUE if window shouldn't be in pager
 * 
 * Since: 2.2
 **/
gboolean
gtk_window_get_skip_pager_hint (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->skips_pager;
}

/**
 * gtk_window_set_urgency_hint:
 * @window: a #GtkWindow 
 * @setting: %TRUE to mark this window as urgent
 * 
 * Windows may set a hint asking the desktop environment to draw
 * the users attention to the window. This function sets this hint.
 * 
 * Since: 2.8
 **/
void
gtk_window_set_urgency_hint (GtkWindow *window,
			     gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->urgent != setting)
    {
      priv->urgent = setting;
      if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gdk_window_set_urgency_hint (gtk_widget_get_window (GTK_WIDGET (window)),
				     priv->urgent);
      g_object_notify (G_OBJECT (window), "urgency-hint");
    }
}

/**
 * gtk_window_get_urgency_hint:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_urgency_hint()
 * 
 * Return value: %TRUE if window is urgent
 * 
 * Since: 2.8
 **/
gboolean
gtk_window_get_urgency_hint (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->urgent;
}

/**
 * gtk_window_set_accept_focus:
 * @window: a #GtkWindow 
 * @setting: %TRUE to let this window receive input focus
 * 
 * Windows may set a hint asking the desktop environment not to receive
 * the input focus. This function sets this hint.
 * 
 * Since: 2.4
 **/
void
gtk_window_set_accept_focus (GtkWindow *window,
			     gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->accept_focus != setting)
    {
      priv->accept_focus = setting;
      if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gdk_window_set_accept_focus (gtk_widget_get_window (GTK_WIDGET (window)),
				     priv->accept_focus);
      g_object_notify (G_OBJECT (window), "accept-focus");
    }
}

/**
 * gtk_window_get_accept_focus:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_accept_focus().
 * 
 * Return value: %TRUE if window should receive the input focus
 * 
 * Since: 2.4
 **/
gboolean
gtk_window_get_accept_focus (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->accept_focus;
}

/**
 * gtk_window_set_focus_on_map:
 * @window: a #GtkWindow 
 * @setting: %TRUE to let this window receive input focus on map
 * 
 * Windows may set a hint asking the desktop environment not to receive
 * the input focus when the window is mapped.  This function sets this
 * hint.
 * 
 * Since: 2.6
 **/
void
gtk_window_set_focus_on_map (GtkWindow *window,
			     gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->focus_on_map != setting)
    {
      priv->focus_on_map = setting;
      if (gtk_widget_get_realized (GTK_WIDGET (window)))
        gdk_window_set_focus_on_map (gtk_widget_get_window (GTK_WIDGET (window)),
				     priv->focus_on_map);
      g_object_notify (G_OBJECT (window), "focus-on-map");
    }
}

/**
 * gtk_window_get_focus_on_map:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_focus_on_map().
 * 
 * Return value: %TRUE if window should receive the input focus when
 * mapped.
 * 
 * Since: 2.6
 **/
gboolean
gtk_window_get_focus_on_map (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->focus_on_map;
}

/**
 * gtk_window_set_destroy_with_parent:
 * @window: a #GtkWindow
 * @setting: whether to destroy @window with its transient parent
 * 
 * If @setting is %TRUE, then destroying the transient parent of @window
 * will also destroy @window itself. This is useful for dialogs that
 * shouldn't persist beyond the lifetime of the main window they're
 * associated with, for example.
 **/
void
gtk_window_set_destroy_with_parent  (GtkWindow *window,
                                     gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (priv->destroy_with_parent == (setting != FALSE))
    return;

  if (priv->destroy_with_parent)
    {
      disconnect_parent_destroyed (window);
    }
  else
    {
      connect_parent_destroyed (window);
    }

  priv->destroy_with_parent = setting;

  g_object_notify (G_OBJECT (window), "destroy-with-parent");
}

/**
 * gtk_window_get_destroy_with_parent:
 * @window: a #GtkWindow
 * 
 * Returns whether the window will be destroyed with its transient parent. See
 * gtk_window_set_destroy_with_parent ().
 *
 * Return value: %TRUE if the window will be destroyed with its transient parent.
 **/
gboolean
gtk_window_get_destroy_with_parent (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->destroy_with_parent;
}

/**
 * gtk_window_set_hide_titlebar_when_maximized:
 * @window: a #GtkWindow
 * @setting: whether to hide the titlebar when @window is maximized
 *
 * If @setting is %TRUE, then @window will request that it's titlebar
 * should be hidden when maximized.
 * This is useful for windows that don't convey any information other
 * than the application name in the titlebar, to put the available
 * screen space to better use. If the underlying window system does not
 * support the request, the setting will not have any effect.
 *
 * Since: 3.4
 **/
void
gtk_window_set_hide_titlebar_when_maximized (GtkWindow *window,
                                             gboolean   setting)
{
  g_return_if_fail (GTK_IS_WINDOW (window));

#ifdef GDK_WINDOWING_X11
  {
    GdkWindow *gdk_window;

    gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

    if (GDK_IS_X11_WINDOW (gdk_window))
      gdk_x11_window_set_hide_titlebar_when_maximized (gdk_window, setting);
  }
#endif

  window->priv->hide_titlebar_when_maximized = setting;
  g_object_notify (G_OBJECT (window), "hide-titlebar-when-maximized");
}

/**
 * gtk_window_get_hide_titlebar_when_maximized:
 * @window: a #GtkWindow
 *
 * Returns whether the window has requested to have its titlebar hidden
 * when maximized. See gtk_window_set_hide_titlebar_when_maximized ().
 *
 * Return value: %TRUE if the window has requested to have its titlebar
 *               hidden when maximized
 *
 * Since: 3.4
 **/
gboolean
gtk_window_get_hide_titlebar_when_maximized (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->hide_titlebar_when_maximized;
}

static GtkWindowGeometryInfo*
gtk_window_get_geometry_info (GtkWindow *window,
			      gboolean   create)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWindowGeometryInfo *info;

  info = priv->geometry_info;
  if (!info && create)
    {
      info = g_new0 (GtkWindowGeometryInfo, 1);

      info->default_width = -1;
      info->default_height = -1;
      info->resize_width = -1;
      info->resize_height = -1;
      info->initial_x = 0;
      info->initial_y = 0;
      info->initial_pos_set = FALSE;
      info->default_is_geometry = FALSE;
      info->position_constraints_changed = FALSE;
      info->last.configure_request.x = 0;
      info->last.configure_request.y = 0;
      info->last.configure_request.width = -1;
      info->last.configure_request.height = -1;
      info->widget = NULL;
      info->mask = 0;
      priv->geometry_info = info;
    }

  return info;
}

/**
 * gtk_window_set_geometry_hints:
 * @window: a #GtkWindow
 * @geometry_widget: (allow-none): widget the geometry hints will be applied to or %NULL
 * @geometry: (allow-none): struct containing geometry information or %NULL
 * @geom_mask: mask indicating which struct fields should be paid attention to
 *
 * This function sets up hints about how a window can be resized by
 * the user.  You can set a minimum and maximum size; allowed resize
 * increments (e.g. for xterm, you can only resize by the size of a
 * character); aspect ratios; and more. See the #GdkGeometry struct.
 * 
 **/
void       
gtk_window_set_geometry_hints (GtkWindow       *window,
			       GtkWidget       *geometry_widget,
			       GdkGeometry     *geometry,
			       GdkWindowHints   geom_mask)
{
  GtkWindowGeometryInfo *info;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (geometry_widget == NULL || GTK_IS_WIDGET (geometry_widget));

  info = gtk_window_get_geometry_info (window, TRUE);
  
  if (info->widget)
    g_signal_handlers_disconnect_by_func (info->widget,
					  gtk_widget_destroyed,
					  &info->widget);
  
  info->widget = geometry_widget;
  if (info->widget)
    g_signal_connect (geometry_widget, "destroy",
		      G_CALLBACK (gtk_widget_destroyed),
		      &info->widget);

  if (geometry)
    info->geometry = *geometry;

  /* We store gravity in priv->gravity not in the hints. */
  info->mask = geom_mask & ~(GDK_HINT_WIN_GRAVITY);

  if (geom_mask & GDK_HINT_WIN_GRAVITY)
    {
      gtk_window_set_gravity (window, geometry->win_gravity);
    }

  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));
}

/**
 * gtk_window_set_decorated:
 * @window: a #GtkWindow
 * @setting: %TRUE to decorate the window
 *
 * By default, windows are decorated with a title bar, resize
 * controls, etc.  Some <link linkend="gtk-X11-arch">window
 * managers</link> allow GTK+ to disable these decorations, creating a
 * borderless window. If you set the decorated property to %FALSE
 * using this function, GTK+ will do its best to convince the window
 * manager not to decorate the window. Depending on the system, this
 * function may not have any effect when called on a window that is
 * already visible, so you should call it before calling gtk_widget_show().
 *
 * On Windows, this function always works, since there's no window manager
 * policy involved.
 * 
 **/
void
gtk_window_set_decorated (GtkWindow *window,
                          gboolean   setting)
{
  GtkWindowPrivate *priv;
  GdkWindow *gdk_window;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (setting == priv->decorated)
    return;

  priv->decorated = setting;

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  if (gdk_window)
    {
      if (priv->decorated)
        gdk_window_set_decorations (gdk_window,
                                    GDK_DECOR_ALL);
      else
        gdk_window_set_decorations (gdk_window,
                                    0);
    }

  g_object_notify (G_OBJECT (window), "decorated");
}

/**
 * gtk_window_get_decorated:
 * @window: a #GtkWindow
 *
 * Returns whether the window has been set to have decorations
 * such as a title bar via gtk_window_set_decorated().
 *
 * Return value: %TRUE if the window has been set to have decorations
 **/
gboolean
gtk_window_get_decorated (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), TRUE);

  return window->priv->decorated;
}

/**
 * gtk_window_set_deletable:
 * @window: a #GtkWindow
 * @setting: %TRUE to decorate the window as deletable
 *
 * By default, windows have a close button in the window frame. Some 
 * <link linkend="gtk-X11-arch">window managers</link> allow GTK+ to 
 * disable this button. If you set the deletable property to %FALSE
 * using this function, GTK+ will do its best to convince the window
 * manager not to show a close button. Depending on the system, this
 * function may not have any effect when called on a window that is
 * already visible, so you should call it before calling gtk_window_show().
 *
 * On Windows, this function always works, since there's no window manager
 * policy involved.
 *
 * Since: 2.10
 */
void
gtk_window_set_deletable (GtkWindow *window,
			  gboolean   setting)
{
  GtkWindowPrivate *priv;
  GdkWindow *gdk_window;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (setting == priv->deletable)
    return;

  priv->deletable = setting;

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));
  if (gdk_window)
    {
      if (priv->deletable)
        gdk_window_set_functions (gdk_window,
				  GDK_FUNC_ALL);
      else
        gdk_window_set_functions (gdk_window,
				  GDK_FUNC_ALL | GDK_FUNC_CLOSE);
    }

  g_object_notify (G_OBJECT (window), "deletable");  
}

/**
 * gtk_window_get_deletable:
 * @window: a #GtkWindow
 *
 * Returns whether the window has been set to have a close button
 * via gtk_window_set_deletable().
 *
 * Return value: %TRUE if the window has been set to have a close button
 *
 * Since: 2.10
 **/
gboolean
gtk_window_get_deletable (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), TRUE);

  return window->priv->deletable;
}

static GtkWindowIconInfo*
get_icon_info (GtkWindow *window)
{
  return g_object_get_qdata (G_OBJECT (window), quark_gtk_window_icon_info);
}
     
static void
free_icon_info (GtkWindowIconInfo *info)
{
  g_free (info->icon_name);
  g_slice_free (GtkWindowIconInfo, info);
}


static GtkWindowIconInfo*
ensure_icon_info (GtkWindow *window)
{
  GtkWindowIconInfo *info;

  info = get_icon_info (window);
  
  if (info == NULL)
    {
      info = g_slice_new0 (GtkWindowIconInfo);
      g_object_set_qdata_full (G_OBJECT (window),
                              quark_gtk_window_icon_info,
                              info,
                              (GDestroyNotify)free_icon_info);
    }

  return info;
}

static GList *
icon_list_from_theme (GtkWidget    *widget,
		      const gchar  *name)
{
  GList *list;

  GtkIconTheme *icon_theme;
  GdkPixbuf *icon;
  gint *sizes;
  gint i;

  icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));

  sizes = gtk_icon_theme_get_icon_sizes (icon_theme, name);

  list = NULL;
  for (i = 0; sizes[i]; i++)
    {      
      /* FIXME
       * We need an EWMH extension to handle scalable icons 
       * by passing their name to the WM. For now just use a 
       * fixed size of 48.
       */ 
      if (sizes[i] == -1)
	icon = gtk_icon_theme_load_icon (icon_theme, name,
					 48, 0, NULL);
      else
	icon = gtk_icon_theme_load_icon (icon_theme, name,
					 sizes[i], 0, NULL);
      if (icon)
	list = g_list_append (list, icon);
    }

  g_free (sizes);

  return list;
}


static void
gtk_window_realize_icon (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *widget;
  GtkWindowIconInfo *info;
  GdkWindow *gdk_window;
  GList *icon_list;

  widget = GTK_WIDGET (window);
  gdk_window = gtk_widget_get_window (widget);

  g_return_if_fail (gdk_window != NULL);

  /* no point setting an icon on override-redirect */
  if (priv->type == GTK_WINDOW_POPUP)
    return;

  icon_list = NULL;
  
  info = ensure_icon_info (window);

  if (info->realized)
    return;

  info->using_default_icon = FALSE;
  info->using_parent_icon = FALSE;
  info->using_themed_icon = FALSE;
  
  icon_list = info->icon_list;

  /* Look up themed icon */
  if (icon_list == NULL && info->icon_name) 
    {
      icon_list = icon_list_from_theme (widget, info->icon_name);
      if (icon_list)
	info->using_themed_icon = TRUE;
    }

  /* Inherit from transient parent */
  if (icon_list == NULL && priv->transient_parent)
    {
      icon_list = ensure_icon_info (priv->transient_parent)->icon_list;
      if (icon_list)
        info->using_parent_icon = TRUE;
    }      

  /* Inherit from default */
  if (icon_list == NULL)
    {
      icon_list = default_icon_list;
      if (icon_list)
        info->using_default_icon = TRUE;
    }

  /* Look up themed icon */
  if (icon_list == NULL && default_icon_name) 
    {
      icon_list = icon_list_from_theme (widget, default_icon_name);
      info->using_default_icon = TRUE;
      info->using_themed_icon = TRUE;  
    }

  info->realized = TRUE;

  gdk_window_set_icon_list (gtk_widget_get_window (widget), icon_list);
  
  if (info->using_themed_icon) 
    {
      GtkIconTheme *icon_theme;

      g_list_free_full (icon_list, g_object_unref);
 
      icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)));
      g_signal_connect (icon_theme, "changed",
			G_CALLBACK (update_themed_icon), window);
    }
}

static void
gtk_window_unrealize_icon (GtkWindow *window)
{
  GtkWindowIconInfo *info;

  info = get_icon_info (window);

  if (info == NULL)
    return;
  
  if (info->using_themed_icon)
    {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)));

      g_signal_handlers_disconnect_by_func (icon_theme, update_themed_icon, window);
    }
    
  /* We don't clear the properties on the window, just figure the
   * window is going away.
   */

  info->realized = FALSE;

}

/**
 * gtk_window_set_icon_list:
 * @window: a #GtkWindow
 * @list: (element-type GdkPixbuf): list of #GdkPixbuf
 *
 * Sets up the icon representing a #GtkWindow. The icon is used when
 * the window is minimized (also known as iconified).  Some window
 * managers or desktop environments may also place it in the window
 * frame, or display it in other contexts.
 *
 * gtk_window_set_icon_list() allows you to pass in the same icon in
 * several hand-drawn sizes. The list should contain the natural sizes
 * your icon is available in; that is, don't scale the image before
 * passing it to GTK+. Scaling is postponed until the last minute,
 * when the desired final size is known, to allow best quality.
 *
 * By passing several sizes, you may improve the final image quality
 * of the icon, by reducing or eliminating automatic image scaling.
 *
 * Recommended sizes to provide: 16x16, 32x32, 48x48 at minimum, and
 * larger images (64x64, 128x128) if you have them.
 *
 * See also gtk_window_set_default_icon_list() to set the icon
 * for all windows in your application in one go.
 *
 * Note that transient windows (those who have been set transient for another
 * window using gtk_window_set_transient_for()) will inherit their
 * icon from their transient parent. So there's no need to explicitly
 * set the icon on transient windows.
 **/
void
gtk_window_set_icon_list (GtkWindow  *window,
                          GList      *list)
{
  GtkWindowIconInfo *info;

  g_return_if_fail (GTK_IS_WINDOW (window));

  info = ensure_icon_info (window);

  if (info->icon_list == list) /* check for NULL mostly */
    return;

  g_list_foreach (list,
                  (GFunc) g_object_ref, NULL);

  g_list_free_full (info->icon_list, g_object_unref);

  info->icon_list = g_list_copy (list);

  g_object_notify (G_OBJECT (window), "icon");
  
  gtk_window_unrealize_icon (window);
  
  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    gtk_window_realize_icon (window);

  /* We could try to update our transient children, but I don't think
   * it's really worth it. If we did it, the best way would probably
   * be to have children connect to notify::icon-list
   */
}

/**
 * gtk_window_get_icon_list:
 * @window: a #GtkWindow
 * 
 * Retrieves the list of icons set by gtk_window_set_icon_list().
 * The list is copied, but the reference count on each
 * member won't be incremented.
 *
 * Return value: (element-type GdkPixbuf) (transfer container): copy of window's icon list
 **/
GList*
gtk_window_get_icon_list (GtkWindow  *window)
{
  GtkWindowIconInfo *info;
  
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  info = get_icon_info (window);

  if (info)
    return g_list_copy (info->icon_list);
  else
    return NULL;  
}

/**
 * gtk_window_set_icon:
 * @window: a #GtkWindow
 * @icon: (allow-none): icon image, or %NULL
 *
 * Sets up the icon representing a #GtkWindow. This icon is used when
 * the window is minimized (also known as iconified).  Some window
 * managers or desktop environments may also place it in the window
 * frame, or display it in other contexts.
 *
 * The icon should be provided in whatever size it was naturally
 * drawn; that is, don't scale the image before passing it to
 * GTK+. Scaling is postponed until the last minute, when the desired
 * final size is known, to allow best quality.
 *
 * If you have your icon hand-drawn in multiple sizes, use
 * gtk_window_set_icon_list(). Then the best size will be used.
 *
 * This function is equivalent to calling gtk_window_set_icon_list()
 * with a 1-element list.
 *
 * See also gtk_window_set_default_icon_list() to set the icon
 * for all windows in your application in one go.
 **/
void
gtk_window_set_icon (GtkWindow  *window,
                     GdkPixbuf  *icon)
{
  GList *list;
  
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (icon == NULL || GDK_IS_PIXBUF (icon));

  list = NULL;

  if (icon)
    list = g_list_append (list, icon);
  
  gtk_window_set_icon_list (window, list);
  g_list_free (list);  
}


static void 
update_themed_icon (GtkIconTheme *icon_theme,
		    GtkWindow    *window)
{
  g_object_notify (G_OBJECT (window), "icon");
  
  gtk_window_unrealize_icon (window);
  
  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    gtk_window_realize_icon (window);  
}

/**
 * gtk_window_set_icon_name:
 * @window: a #GtkWindow
 * @name: (allow-none): the name of the themed icon
 *
 * Sets the icon for the window from a named themed icon. See
 * the docs for #GtkIconTheme for more details.
 *
 * Note that this has nothing to do with the WM_ICON_NAME 
 * property which is mentioned in the ICCCM.
 *
 * Since: 2.6
 */
void 
gtk_window_set_icon_name (GtkWindow   *window,
			  const gchar *name)
{
  GtkWindowIconInfo *info;
  gchar *tmp;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  info = ensure_icon_info (window);

  if (g_strcmp0 (info->icon_name, name) == 0)
    return;

  tmp = info->icon_name;
  info->icon_name = g_strdup (name);
  g_free (tmp);

  g_list_free_full (info->icon_list, g_object_unref);
  info->icon_list = NULL;
  
  update_themed_icon (NULL, window);

  g_object_notify (G_OBJECT (window), "icon-name");
}

/**
 * gtk_window_get_icon_name:
 * @window: a #GtkWindow
 *
 * Returns the name of the themed icon for the window,
 * see gtk_window_set_icon_name().
 *
 * Returns: the icon name or %NULL if the window has 
 * no themed icon
 *
 * Since: 2.6
 */
const gchar *
gtk_window_get_icon_name (GtkWindow *window)
{
  GtkWindowIconInfo *info;

  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  info = ensure_icon_info (window);

  return info->icon_name;
}

/**
 * gtk_window_get_icon:
 * @window: a #GtkWindow
 * 
 * Gets the value set by gtk_window_set_icon() (or if you've
 * called gtk_window_set_icon_list(), gets the first icon in
 * the icon list).
 *
 * Return value: (transfer none): icon for window
 **/
GdkPixbuf*
gtk_window_get_icon (GtkWindow  *window)
{
  GtkWindowIconInfo *info;

  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  info = get_icon_info (window);
  if (info && info->icon_list)
    return GDK_PIXBUF (info->icon_list->data);
  else
    return NULL;
}

/* Load pixbuf, printing warning on failure if error == NULL
 */
static GdkPixbuf *
load_pixbuf_verbosely (const char *filename,
		       GError    **err)
{
  GError *local_err = NULL;
  GdkPixbuf *pixbuf;

  pixbuf = gdk_pixbuf_new_from_file (filename, &local_err);

  if (!pixbuf)
    {
      if (err)
	*err = local_err;
      else
	{
	  g_warning ("Error loading icon from file '%s':\n\t%s",
		     filename, local_err->message);
	  g_error_free (local_err);
	}
    }

  return pixbuf;
}

/**
 * gtk_window_set_icon_from_file:
 * @window: a #GtkWindow
 * @filename: (type filename): location of icon file
 * @err: (allow-none): location to store error, or %NULL.
 *
 * Sets the icon for @window.  
 * Warns on failure if @err is %NULL.
 *
 * This function is equivalent to calling gtk_window_set_icon()
 * with a pixbuf created by loading the image from @filename.
 *
 * Returns: %TRUE if setting the icon succeeded.
 *
 * Since: 2.2
 **/
gboolean
gtk_window_set_icon_from_file (GtkWindow   *window,
			       const gchar *filename,
			       GError     **err)
{
  GdkPixbuf *pixbuf = load_pixbuf_verbosely (filename, err);

  if (pixbuf)
    {
      gtk_window_set_icon (window, pixbuf);
      g_object_unref (pixbuf);
      
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * gtk_window_set_default_icon_list:
 * @list: (element-type GdkPixbuf) (transfer container): a list of #GdkPixbuf
 *
 * Sets an icon list to be used as fallback for windows that haven't
 * had gtk_window_set_icon_list() called on them to set up a
 * window-specific icon list. This function allows you to set up the
 * icon for all windows in your app at once.
 *
 * See gtk_window_set_icon_list() for more details.
 * 
 **/
void
gtk_window_set_default_icon_list (GList *list)
{
  GList *toplevels;
  GList *tmp_list;
  if (list == default_icon_list)
    return;

  /* Update serial so we don't used cached pixmaps/masks
   */
  default_icon_serial++;
  
  g_list_foreach (list,
                  (GFunc) g_object_ref, NULL);

  g_list_free_full (default_icon_list, g_object_unref);

  default_icon_list = g_list_copy (list);
  
  /* Update all toplevels */
  toplevels = gtk_window_list_toplevels ();
  tmp_list = toplevels;
  while (tmp_list != NULL)
    {
      GtkWindowIconInfo *info;
      GtkWindow *w = tmp_list->data;
      
      info = get_icon_info (w);
      if (info && info->using_default_icon)
        {
          gtk_window_unrealize_icon (w);
          if (gtk_widget_get_realized (GTK_WIDGET (w)))
            gtk_window_realize_icon (w);
        }

      tmp_list = tmp_list->next;
    }
  g_list_free (toplevels);
}

/**
 * gtk_window_set_default_icon:
 * @icon: the icon
 *
 * Sets an icon to be used as fallback for windows that haven't
 * had gtk_window_set_icon() called on them from a pixbuf.
 *
 * Since: 2.4
 **/
void
gtk_window_set_default_icon (GdkPixbuf *icon)
{
  GList *list;
  
  g_return_if_fail (GDK_IS_PIXBUF (icon));

  list = g_list_prepend (NULL, icon);
  gtk_window_set_default_icon_list (list);
  g_list_free (list);
}

/**
 * gtk_window_set_default_icon_name:
 * @name: the name of the themed icon
 *
 * Sets an icon to be used as fallback for windows that haven't
 * had gtk_window_set_icon_list() called on them from a named
 * themed icon, see gtk_window_set_icon_name().
 *
 * Since: 2.6
 **/
void
gtk_window_set_default_icon_name (const gchar *name)
{
  GList *tmp_list;
  GList *toplevels;

  /* Update serial so we don't used cached pixmaps/masks
   */
  default_icon_serial++;

  g_free (default_icon_name);
  default_icon_name = g_strdup (name);

  g_list_free_full (default_icon_list, g_object_unref);
  default_icon_list = NULL;
  
  /* Update all toplevels */
  toplevels = gtk_window_list_toplevels ();
  tmp_list = toplevels;
  while (tmp_list != NULL)
    {
      GtkWindowIconInfo *info;
      GtkWindow *w = tmp_list->data;
      
      info = get_icon_info (w);
      if (info && info->using_default_icon && info->using_themed_icon)
        {
          gtk_window_unrealize_icon (w);
          if (gtk_widget_get_realized (GTK_WIDGET (w)))
            gtk_window_realize_icon (w);
        }

      tmp_list = tmp_list->next;
    }
  g_list_free (toplevels);
}

/**
 * gtk_window_get_default_icon_name:
 *
 * Returns the fallback icon name for windows that has been set
 * with gtk_window_set_default_icon_name(). The returned
 * string is owned by GTK+ and should not be modified. It
 * is only valid until the next call to
 * gtk_window_set_default_icon_name().
 *
 * Returns: the fallback icon name for windows
 *
 * Since: 2.16
 */
const gchar *
gtk_window_get_default_icon_name (void)
{
  return default_icon_name;
}

/**
 * gtk_window_set_default_icon_from_file:
 * @filename: (type filename): location of icon file
 * @err: (allow-none): location to store error, or %NULL.
 *
 * Sets an icon to be used as fallback for windows that haven't
 * had gtk_window_set_icon_list() called on them from a file
 * on disk. Warns on failure if @err is %NULL.
 *
 * Returns: %TRUE if setting the icon succeeded.
 *
 * Since: 2.2
 **/
gboolean
gtk_window_set_default_icon_from_file (const gchar *filename,
				       GError     **err)
{
  GdkPixbuf *pixbuf = load_pixbuf_verbosely (filename, err);

  if (pixbuf)
    {
      gtk_window_set_default_icon (pixbuf);
      g_object_unref (pixbuf);
      
      return TRUE;
    }
  else
    return FALSE;
}

/**
 * gtk_window_get_default_icon_list:
 * 
 * Gets the value set by gtk_window_set_default_icon_list().
 * The list is a copy and should be freed with g_list_free(),
 * but the pixbufs in the list have not had their reference count
 * incremented.
 * 
 * Return value: (element-type GdkPixbuf) (transfer container): copy of default icon list 
 **/
GList*
gtk_window_get_default_icon_list (void)
{
  return g_list_copy (default_icon_list);
}

static void
gtk_window_set_default_size_internal (GtkWindow    *window,
                                      gboolean      change_width,
                                      gint          width,
                                      gboolean      change_height,
                                      gint          height,
				      gboolean      is_geometry)
{
  GtkWindowGeometryInfo *info;

  g_return_if_fail (change_width == FALSE || width >= -1);
  g_return_if_fail (change_height == FALSE || height >= -1);

  info = gtk_window_get_geometry_info (window, TRUE);

  g_object_freeze_notify (G_OBJECT (window));

  info->default_is_geometry = is_geometry != FALSE;

  if (change_width)
    {
      if (width == 0)
        width = 1;

      if (width < 0)
        width = -1;

      info->default_width = width;

      g_object_notify (G_OBJECT (window), "default-width");
    }

  if (change_height)
    {
      if (height == 0)
        height = 1;

      if (height < 0)
        height = -1;

      info->default_height = height;
      
      g_object_notify (G_OBJECT (window), "default-height");
    }
  
  g_object_thaw_notify (G_OBJECT (window));
  
  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));
}

/**
 * gtk_window_set_default_size:
 * @window: a #GtkWindow
 * @width: width in pixels, or -1 to unset the default width
 * @height: height in pixels, or -1 to unset the default height
 *
 * Sets the default size of a window. If the window's "natural" size
 * (its size request) is larger than the default, the default will be
 * ignored. More generally, if the default size does not obey the
 * geometry hints for the window (gtk_window_set_geometry_hints() can
 * be used to set these explicitly), the default size will be clamped
 * to the nearest permitted size.
 * 
 * Unlike gtk_widget_set_size_request(), which sets a size request for
 * a widget and thus would keep users from shrinking the window, this
 * function only sets the initial size, just as if the user had
 * resized the window themselves. Users can still shrink the window
 * again as they normally would. Setting a default size of -1 means to
 * use the "natural" default size (the size request of the window).
 *
 * For more control over a window's initial size and how resizing works,
 * investigate gtk_window_set_geometry_hints().
 *
 * For some uses, gtk_window_resize() is a more appropriate function.
 * gtk_window_resize() changes the current size of the window, rather
 * than the size to be used on initial display. gtk_window_resize() always
 * affects the window itself, not the geometry widget.
 *
 * The default size of a window only affects the first time a window is
 * shown; if a window is hidden and re-shown, it will remember the size
 * it had prior to hiding, rather than using the default size.
 *
 * Windows can't actually be 0x0 in size, they must be at least 1x1, but
 * passing 0 for @width and @height is OK, resulting in a 1x1 default size.
 **/
void       
gtk_window_set_default_size (GtkWindow   *window,
			     gint         width,
			     gint         height)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (width >= -1);
  g_return_if_fail (height >= -1);

  gtk_window_set_default_size_internal (window, TRUE, width, TRUE, height, FALSE);
}

/**
 * gtk_window_set_default_geometry:
 * @window: a #GtkWindow
 * @width: width in resize increments, or -1 to unset the default width
 * @height: height in resize increments, or -1 to unset the default height
 *
 * Like gtk_window_set_default_size(), but @width and @height are interpreted
 * in terms of the base size and increment set with
 * gtk_window_set_geometry_hints.
 *
 * Since: 3.0
 */
void
gtk_window_set_default_geometry (GtkWindow *window,
				 gint       width,
				 gint       height)
{
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (width >= -1);
  g_return_if_fail (height >= -1);

  gtk_window_set_default_size_internal (window, TRUE, width, TRUE, height, TRUE);
}

/**
 * gtk_window_get_default_size:
 * @window: a #GtkWindow
 * @width: (out) (allow-none): location to store the default width, or %NULL
 * @height: (out) (allow-none): location to store the default height, or %NULL
 *
 * Gets the default size of the window. A value of -1 for the width or
 * height indicates that a default size has not been explicitly set
 * for that dimension, so the "natural" size of the window will be
 * used.
 * 
 **/
void
gtk_window_get_default_size (GtkWindow *window,
			     gint      *width,
			     gint      *height)
{
  GtkWindowGeometryInfo *info;

  g_return_if_fail (GTK_IS_WINDOW (window));

  info = gtk_window_get_geometry_info (window, FALSE);

  if (width)
    *width = info ? info->default_width : -1;

  if (height)
    *height = info ? info->default_height : -1;
}

/**
 * gtk_window_resize:
 * @window: a #GtkWindow
 * @width: width in pixels to resize the window to
 * @height: height in pixels to resize the window to
 *
 * Resizes the window as if the user had done so, obeying geometry
 * constraints. The default geometry constraint is that windows may
 * not be smaller than their size request; to override this
 * constraint, call gtk_widget_set_size_request() to set the window's
 * request to a smaller value.
 *
 * If gtk_window_resize() is called before showing a window for the
 * first time, it overrides any default size set with
 * gtk_window_set_default_size().
 *
 * Windows may not be resized smaller than 1 by 1 pixels.
 * 
 **/
void
gtk_window_resize (GtkWindow *window,
                   gint       width,
                   gint       height)
{
  GtkWindowGeometryInfo *info;
  
  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);

  info = gtk_window_get_geometry_info (window, TRUE);

  info->resize_width = width;
  info->resize_height = height;
  info->resize_is_geometry = FALSE;

  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));
}

/**
 * gtk_window_resize_to_geometry:
 * @window: a #GtkWindow
 * @width: width in resize increments to resize the window to
 * @height: height in resize increments to resize the window to
 *
 * Like gtk_window_resize(), but @width and @height are interpreted
 * in terms of the base size and increment set with
 * gtk_window_set_geometry_hints.
 *
 * Since: 3.0
 */
void
gtk_window_resize_to_geometry (GtkWindow *window,
			       gint       width,
			       gint       height)
{
  GtkWindowGeometryInfo *info;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (width > 0);
  g_return_if_fail (height > 0);

  info = gtk_window_get_geometry_info (window, TRUE);

  info->resize_width = width;
  info->resize_height = height;
  info->resize_is_geometry = TRUE;

  gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));
}

/**
 * gtk_window_get_size:
 * @window: a #GtkWindow
 * @width: (out) (allow-none): return location for width, or %NULL
 * @height: (out) (allow-none): return location for height, or %NULL
 *
 * Obtains the current size of @window. If @window is not onscreen,
 * it returns the size GTK+ will suggest to the <link
 * linkend="gtk-X11-arch">window manager</link> for the initial window
 * size (but this is not reliably the same as the size the window
 * manager will actually select). The size obtained by
 * gtk_window_get_size() is the last size received in a
 * #GdkEventConfigure, that is, GTK+ uses its locally-stored size,
 * rather than querying the X server for the size. As a result, if you
 * call gtk_window_resize() then immediately call
 * gtk_window_get_size(), the size won't have taken effect yet. After
 * the window manager processes the resize request, GTK+ receives
 * notification that the size has changed via a configure event, and
 * the size of the window gets updated.
 *
 * Note 1: Nearly any use of this function creates a race condition,
 * because the size of the window may change between the time that you
 * get the size and the time that you perform some action assuming
 * that size is the current size. To avoid race conditions, connect to
 * "configure-event" on the window and adjust your size-dependent
 * state to match the size delivered in the #GdkEventConfigure.
 *
 * Note 2: The returned size does <emphasis>not</emphasis> include the
 * size of the window manager decorations (aka the window frame or
 * border). Those are not drawn by GTK+ and GTK+ has no reliable
 * method of determining their size.
 *
 * Note 3: If you are getting a window size in order to position
 * the window onscreen, there may be a better way. The preferred
 * way is to simply set the window's semantic type with
 * gtk_window_set_type_hint(), which allows the window manager to
 * e.g. center dialogs. Also, if you set the transient parent of
 * dialogs with gtk_window_set_transient_for() window managers
 * will often center the dialog over its parent window. It's
 * much preferred to let the window manager handle these
 * things rather than doing it yourself, because all apps will
 * behave consistently and according to user prefs if the window
 * manager handles it. Also, the window manager can take the size
 * of the window decorations/border into account, while your
 * application cannot.
 *
 * In any case, if you insist on application-specified window
 * positioning, there's <emphasis>still</emphasis> a better way than
 * doing it yourself - gtk_window_set_position() will frequently
 * handle the details for you.
 * 
 **/
void
gtk_window_get_size (GtkWindow *window,
                     gint      *width,
                     gint      *height)
{
  gint w, h;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  if (width == NULL && height == NULL)
    return;

  if (gtk_widget_get_mapped (GTK_WIDGET (window)))
    {
      w = gdk_window_get_width (gtk_widget_get_window (GTK_WIDGET (window)));
      h = gdk_window_get_height (gtk_widget_get_window (GTK_WIDGET (window)));
    }
  else
    {
      GdkRectangle configure_request;

      gtk_window_compute_configure_request (window,
                                            &configure_request,
                                            NULL, NULL);

      w = configure_request.width;
      h = configure_request.height;
    }
  
  if (width)
    *width = w;
  if (height)
    *height = h;
}

/**
 * gtk_window_move:
 * @window: a #GtkWindow
 * @x: X coordinate to move window to
 * @y: Y coordinate to move window to
 *
 * Asks the <link linkend="gtk-X11-arch">window manager</link> to move
 * @window to the given position.  Window managers are free to ignore
 * this; most window managers ignore requests for initial window
 * positions (instead using a user-defined placement algorithm) and
 * honor requests after the window has already been shown.
 *
 * Note: the position is the position of the gravity-determined
 * reference point for the window. The gravity determines two things:
 * first, the location of the reference point in root window
 * coordinates; and second, which point on the window is positioned at
 * the reference point.
 *
 * By default the gravity is #GDK_GRAVITY_NORTH_WEST, so the reference
 * point is simply the @x, @y supplied to gtk_window_move(). The
 * top-left corner of the window decorations (aka window frame or
 * border) will be placed at @x, @y.  Therefore, to position a window
 * at the top left of the screen, you want to use the default gravity
 * (which is #GDK_GRAVITY_NORTH_WEST) and move the window to 0,0.
 *
 * To position a window at the bottom right corner of the screen, you
 * would set #GDK_GRAVITY_SOUTH_EAST, which means that the reference
 * point is at @x + the window width and @y + the window height, and
 * the bottom-right corner of the window border will be placed at that
 * reference point. So, to place a window in the bottom right corner
 * you would first set gravity to south east, then write:
 * <literal>gtk_window_move (window, gdk_screen_width () - window_width,
 * gdk_screen_height () - window_height)</literal> (note that this
 * example does not take multi-head scenarios into account).
 *
 * The Extended Window Manager Hints specification at <ulink
 * url="http://www.freedesktop.org/Standards/wm-spec">
 * http://www.freedesktop.org/Standards/wm-spec</ulink> has a
 * nice table of gravities in the "implementation notes" section.
 *
 * The gtk_window_get_position() documentation may also be relevant.
 */
void
gtk_window_move (GtkWindow *window,
                 gint       x,
                 gint       y)
{
  GtkWindowGeometryInfo *info;
  GtkWidget *widget;

  g_return_if_fail (GTK_IS_WINDOW (window));

  widget = GTK_WIDGET (window);

  info = gtk_window_get_geometry_info (window, TRUE);  

  if (gtk_widget_get_mapped (widget))
    {
      GtkAllocation allocation;

      gtk_widget_get_allocation (widget, &allocation);

      /* we have now sent a request with this position
       * with currently-active constraints, so toggle flag.
       */
      info->position_constraints_changed = FALSE;

      /* we only constrain if mapped - if not mapped,
       * then gtk_window_compute_configure_request()
       * will apply the constraints later, and we
       * don't want to lose information about
       * what position the user set before then.
       * i.e. if you do a move() then turn off POS_CENTER
       * then show the window, your move() will work.
       */
      gtk_window_constrain_position (window,
                                     allocation.width, allocation.height,
                                     &x, &y);

      /* Note that this request doesn't go through our standard request
       * framework, e.g. doesn't increment configure_request_count,
       * doesn't set info->last, etc.; that's because
       * we don't save the info needed to arrive at this same request
       * again.
       *
       * To gtk_window_move_resize(), this will end up looking exactly
       * the same as the position being changed by the window
       * manager.
       */
      gdk_window_move (gtk_widget_get_window (GTK_WIDGET (window)), x, y);
    }
  else
    {
      /* Save this position to apply on mapping */
      info->initial_x = x;
      info->initial_y = y;
      info->initial_pos_set = TRUE;
    }
}

/**
 * gtk_window_get_position:
 * @window: a #GtkWindow
 * @root_x: (out) (allow-none): eturn location for X coordinate of
 *     gravity-determined reference point, or %NULL
 * @root_y: (out) (allow-none): return location for Y coordinate of
 *     gravity-determined reference point, or %NULL
 *
 * This function returns the position you need to pass to
 * gtk_window_move() to keep @window in its current position.
 * This means that the meaning of the returned value varies with
 * window gravity. See gtk_window_move() for more details.
 *
 * If you haven't changed the window gravity, its gravity will be
 * #GDK_GRAVITY_NORTH_WEST. This means that gtk_window_get_position()
 * gets the position of the top-left corner of the window manager
 * frame for the window. gtk_window_move() sets the position of this
 * same top-left corner.
 *
 * gtk_window_get_position() is not 100% reliable because the X Window System
 * does not specify a way to obtain the geometry of the
 * decorations placed on a window by the window manager.
 * Thus GTK+ is using a "best guess" that works with most
 * window managers.
 *
 * Moreover, nearly all window managers are historically broken with
 * respect to their handling of window gravity. So moving a window to
 * its current position as returned by gtk_window_get_position() tends
 * to result in moving the window slightly. Window managers are
 * slowly getting better over time.
 *
 * If a window has gravity #GDK_GRAVITY_STATIC the window manager
 * frame is not relevant, and thus gtk_window_get_position() will
 * always produce accurate results. However you can't use static
 * gravity to do things like place a window in a corner of the screen,
 * because static gravity ignores the window manager decorations.
 *
 * If you are saving and restoring your application's window
 * positions, you should know that it's impossible for applications to
 * do this without getting it somewhat wrong because applications do
 * not have sufficient knowledge of window manager state. The Correct
 * Mechanism is to support the session management protocol (see the
 * "GnomeClient" object in the GNOME libraries for example) and allow
 * the window manager to save your window sizes and positions.
 * 
 **/

void
gtk_window_get_position (GtkWindow *window,
                         gint      *root_x,
                         gint      *root_y)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *gdk_window;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);
  gdk_window = gtk_widget_get_window (widget);

  if (priv->gravity == GDK_GRAVITY_STATIC)
    {
      if (gtk_widget_get_mapped (widget))
        {
          /* This does a server round-trip, which is sort of wrong;
           * but a server round-trip is inevitable for
           * gdk_window_get_frame_extents() in the usual
           * NorthWestGravity case below, so not sure what else to
           * do. We should likely be consistent about whether we get
           * the client-side info or the server-side info.
           */
          gdk_window_get_origin (gdk_window, root_x, root_y);
        }
      else
        {
          GdkRectangle configure_request;
          
          gtk_window_compute_configure_request (window,
                                                &configure_request,
                                                NULL, NULL);
          
          *root_x = configure_request.x;
          *root_y = configure_request.y;
        }
    }
  else
    {
      GdkRectangle frame_extents;
      
      gint x, y;
      gint w, h;
      
      if (gtk_widget_get_mapped (widget))
        {
          gdk_window_get_frame_extents (gdk_window, &frame_extents);
          x = frame_extents.x;
          y = frame_extents.y;
          gtk_window_get_size (window, &w, &h);
        }
      else
        {
          /* We just say the frame has 0 size on all sides.
           * Not sure what else to do.
           */
          gtk_window_compute_configure_request (window,
                                                &frame_extents,
                                                NULL, NULL);
          x = frame_extents.x;
          y = frame_extents.y;
          w = frame_extents.width;
          h = frame_extents.height;
        }
      
      switch (priv->gravity)
        {
        case GDK_GRAVITY_NORTH:
        case GDK_GRAVITY_CENTER:
        case GDK_GRAVITY_SOUTH:
          /* Find center of frame. */
          x += frame_extents.width / 2;
          /* Center client window on that point. */
          x -= w / 2;
          break;

        case GDK_GRAVITY_SOUTH_EAST:
        case GDK_GRAVITY_EAST:
        case GDK_GRAVITY_NORTH_EAST:
          /* Find right edge of frame */
          x += frame_extents.width;
          /* Align left edge of client at that point. */
          x -= w;
          break;
        default:
          break;
        }

      switch (priv->gravity)
        {
        case GDK_GRAVITY_WEST:
        case GDK_GRAVITY_CENTER:
        case GDK_GRAVITY_EAST:
          /* Find center of frame. */
          y += frame_extents.height / 2;
          /* Center client window there. */
          y -= h / 2;
          break;
        case GDK_GRAVITY_SOUTH_WEST:
        case GDK_GRAVITY_SOUTH:
        case GDK_GRAVITY_SOUTH_EAST:
          /* Find south edge of frame */
          y += frame_extents.height;
          /* Place bottom edge of client there */
          y -= h;
          break;
        default:
          break;
        }
      
      if (root_x)
        *root_x = x;
      if (root_y)
        *root_y = y;
    }
}

/**
 * gtk_window_reshow_with_initial_size:
 * @window: a #GtkWindow
 * 
 * Hides @window, then reshows it, resetting the
 * default size and position of the window. Used
 * by GUI builders only.
 **/
void
gtk_window_reshow_with_initial_size (GtkWindow *window)
{
  GtkWidget *widget;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  widget = GTK_WIDGET (window);
  
  gtk_widget_hide (widget);
  gtk_widget_unrealize (widget);
  gtk_widget_show (widget);
}

static void
gtk_window_destroy (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;

  gtk_window_release_application (window);

  toplevel_list = g_slist_remove (toplevel_list, window);

  if (priv->transient_parent)
    gtk_window_set_transient_for (window, NULL);

  remove_attach_widget (GTK_WINDOW (widget));

  /* frees the icons */
  gtk_window_set_icon_list (window, NULL);

  if (priv->has_user_ref_count)
    {
      priv->has_user_ref_count = FALSE;
      g_object_unref (window);
    }

  if (priv->group)
    gtk_window_group_remove_window (priv->group, window);

   gtk_window_free_key_hash (window);

  GTK_WIDGET_CLASS (gtk_window_parent_class)->destroy (widget);
}

static void
gtk_window_finalize (GObject *object)
{
  GtkWindow *window = GTK_WINDOW (object);
  GtkWindowPrivate *priv = window->priv;
  GtkMnemonicHash *mnemonic_hash;

  g_free (priv->title);
  g_free (priv->wmclass_name);
  g_free (priv->wmclass_class);
  g_free (priv->wm_role);
  gtk_window_release_application (window);

  mnemonic_hash = gtk_window_get_mnemonic_hash (window, FALSE);
  if (mnemonic_hash)
    _gtk_mnemonic_hash_free (mnemonic_hash);

  if (priv->geometry_info)
    {
      if (priv->geometry_info->widget)
	g_signal_handlers_disconnect_by_func (priv->geometry_info->widget,
					      gtk_widget_destroyed,
					      &priv->geometry_info->widget);
      g_free (priv->geometry_info);
    }

  if (priv->keys_changed_handler)
    {
      g_source_remove (priv->keys_changed_handler);
      priv->keys_changed_handler = 0;
    }

  if (priv->screen)
    g_signal_handlers_disconnect_by_func (priv->screen,
                                          gtk_window_on_composited_changed, window);

  g_free (priv->startup_id);

  if (priv->auto_mnemonics_timeout_id)
    {
      g_source_remove (priv->auto_mnemonics_timeout_id);
      priv->auto_mnemonics_timeout_id = 0;
    }

#ifdef GDK_WINDOWING_X11
  g_signal_handlers_disconnect_by_func (gtk_settings_get_default (),
                                        gtk_window_on_theme_variant_changed,
                                        window);
#endif

  G_OBJECT_CLASS (gtk_window_parent_class)->finalize (object);
}

static void
gtk_window_show (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  GtkContainer *container = GTK_CONTAINER (window);
  GtkBitmask *empty;
  gboolean need_resize;
  gboolean is_plug;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    {
      GTK_WIDGET_CLASS (gtk_window_parent_class)->show (widget);
      return;
    }

  _gtk_widget_set_visible_flag (widget, TRUE);

  need_resize = _gtk_widget_get_alloc_needed (widget) || !gtk_widget_get_realized (widget);

  empty = _gtk_bitmask_new ();
  _gtk_style_context_validate (gtk_widget_get_style_context (widget),
                               g_get_monotonic_time (),
                               0,
                               empty);
  _gtk_bitmask_free (empty);

  if (need_resize)
    {
      GtkWindowGeometryInfo *info = gtk_window_get_geometry_info (window, TRUE);
      GtkAllocation allocation = { 0, 0 };
      GdkRectangle configure_request;
      GdkGeometry new_geometry;
      guint new_flags;
      gboolean was_realized;

      /* We are going to go ahead and perform this configure request
       * and then emulate a configure notify by going ahead and
       * doing a size allocate. Sort of a synchronous
       * mini-copy of gtk_window_move_resize() here.
       */
      gtk_window_compute_configure_request (window,
                                            &configure_request,
                                            &new_geometry,
                                            &new_flags);
      
      /* We update this because we are going to go ahead
       * and gdk_window_resize() below, rather than
       * queuing it.
       */
      info->last.configure_request.width = configure_request.width;
      info->last.configure_request.height = configure_request.height;
      
      /* and allocate the window - this is normally done
       * in move_resize in response to configure notify
       */
      allocation.width  = configure_request.width;
      allocation.height = configure_request.height;
      gtk_widget_size_allocate (widget, &allocation);

      /* Then we guarantee we have a realize */
      was_realized = FALSE;
      if (!gtk_widget_get_realized (widget))
	{
	  gtk_widget_realize (widget);
	  was_realized = TRUE;
	}

      /* We only send configure request if we didn't just finish
       * creating the window; if we just created the window
       * then we created it with widget->allocation anyhow.
       */
      if (!was_realized)
        gdk_window_move_resize (gtk_widget_get_window (widget),
				configure_request.x,
				configure_request.y,
				configure_request.width,
				configure_request.height);
    }
  
  gtk_container_check_resize (container);

  gtk_widget_map (widget);

  /* Try to make sure that we have some focused widget
   */
#ifdef GDK_WINDOWING_X11
  is_plug = GDK_IS_X11_WINDOW (gtk_widget_get_window (widget)) &&
    GTK_IS_PLUG (window);
#else
  is_plug = FALSE;
#endif
  if (!priv->focus_widget && !is_plug)
    gtk_window_move_focus (widget, GTK_DIR_TAB_FORWARD);
  
  if (priv->modal)
    gtk_grab_add (widget);
}

static void
gtk_window_hide (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    {
      GTK_WIDGET_CLASS (gtk_window_parent_class)->hide (widget);
      return;
    }

  _gtk_widget_set_visible_flag (widget, FALSE);
  gtk_widget_unmap (widget);

  if (priv->modal)
    gtk_grab_remove (widget);
}

static void
gtk_window_map (GtkWidget *widget)
{
  GtkWidget *child;
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  GdkWindow *gdk_window;
  gboolean auto_mnemonics;
  GtkPolicyType visible_focus;

  if (!gtk_widget_is_toplevel (widget))
    {
      GTK_WIDGET_CLASS (gtk_window_parent_class)->map (widget);
      return;
    }

  gtk_widget_set_mapped (widget, TRUE);

  child = gtk_bin_get_child (&(window->bin));
  if (child &&
      gtk_widget_get_visible (child) &&
      !gtk_widget_get_mapped (child))
    gtk_widget_map (child);

  gdk_window = gtk_widget_get_window (widget);

  if (priv->maximize_initially)
    gdk_window_maximize (gdk_window);
  else
    gdk_window_unmaximize (gdk_window);

  if (priv->stick_initially)
    gdk_window_stick (gdk_window);
  else
    gdk_window_unstick (gdk_window);

  if (priv->iconify_initially)
    gdk_window_iconify (gdk_window);
  else
    gdk_window_deiconify (gdk_window);

  if (priv->fullscreen_initially)
    gdk_window_fullscreen (gdk_window);
  else
    gdk_window_unfullscreen (gdk_window);

  gdk_window_set_keep_above (gdk_window, priv->above_initially);

  gdk_window_set_keep_below (gdk_window, priv->below_initially);

  if (priv->type == GTK_WINDOW_TOPLEVEL)
    {
      gtk_window_set_theme_variant (window);
      gtk_window_set_hide_titlebar_when_maximized (window,
                                                   priv->hide_titlebar_when_maximized);
    }

  /* No longer use the default settings */
  priv->need_default_size = FALSE;
  priv->need_default_position = FALSE;

  if (priv->reset_type_hint)
    {
      /* We should only reset the type hint when the application
       * used gtk_window_set_type_hint() to change the hint.
       * Some applications use X directly to change the properties;
       * in that case, we shouldn't overwrite what they did.
       */
      gdk_window_set_type_hint (gdk_window, priv->gdk_type_hint);
      priv->reset_type_hint = FALSE;
    }

  gdk_window_show (gdk_window);

  if (priv->grip_window)
    gdk_window_show (priv->grip_window);

  if (!disable_startup_notification)
    {
      /* Do we have a custom startup-notification id? */
      if (priv->startup_id != NULL)
        {
          /* Make sure we have a "real" id */
          if (!startup_id_is_fake (priv->startup_id))
            gdk_notify_startup_complete_with_id (priv->startup_id);

          g_free (priv->startup_id);
          priv->startup_id = NULL;
        }
      else if (!sent_startup_notification)
        {
          sent_startup_notification = TRUE;
          gdk_notify_startup_complete ();
        }
    }

  /* if auto-mnemonics is enabled and mnemonics visible is not already set
   * (as in the case of popup menus), then hide mnemonics initially
   */
  g_object_get (gtk_widget_get_settings (widget),
                "gtk-auto-mnemonics", &auto_mnemonics,
                "gtk-visible-focus", &visible_focus,
                NULL);

  if (auto_mnemonics && !priv->mnemonics_visible_set)
    gtk_window_set_mnemonics_visible (window, FALSE);

  /* inherit from transient parent, so that a dialog that is
   * opened via keynav shows focus initially
   */
  if (priv->transient_parent)
    gtk_window_set_focus_visible (window, gtk_window_get_focus_visible (priv->transient_parent));
  else
    gtk_window_set_focus_visible (window, visible_focus == GTK_POLICY_ALWAYS);
}

static gboolean
gtk_window_map_event (GtkWidget   *widget,
                      GdkEventAny *event)
{
  if (!gtk_widget_get_mapped (widget))
    {
      /* we should be be unmapped, but are getting a MapEvent, this may happen
       * to toplevel XWindows if mapping was intercepted by a window manager
       * and an unmap request occoured while the MapRequestEvent was still
       * being handled. we work around this situaiton here by re-requesting
       * the window being unmapped. more details can be found in:
       *   http://bugzilla.gnome.org/show_bug.cgi?id=316180
       */
      gdk_window_hide (gtk_widget_get_window (widget));
    }
  return FALSE;
}

static void
gtk_window_unmap (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *child;
  GtkWindowGeometryInfo *info;
  GdkWindow *gdk_window;
  GdkWindowState state;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    {
      GTK_WIDGET_CLASS (gtk_window_parent_class)->unmap (widget);
      return;
    }

  gdk_window = gtk_widget_get_window (widget);

  gtk_widget_set_mapped (widget, FALSE);
  gdk_window_withdraw (gdk_window);

  priv->configure_request_count = 0;
  priv->configure_notify_received = FALSE;

  /* on unmap, we reset the default positioning of the window,
   * so it's placed again, but we don't reset the default
   * size of the window, so it's remembered.
   */
  priv->need_default_position = TRUE;

  info = gtk_window_get_geometry_info (window, FALSE);
  if (info)
    {
      info->initial_pos_set = FALSE;
      info->position_constraints_changed = FALSE;
    }

  state = gdk_window_get_state (gdk_window);
  priv->iconify_initially = (state & GDK_WINDOW_STATE_ICONIFIED) != 0;
  priv->maximize_initially = (state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  priv->stick_initially = (state & GDK_WINDOW_STATE_STICKY) != 0;
  priv->above_initially = (state & GDK_WINDOW_STATE_ABOVE) != 0;
  priv->below_initially = (state & GDK_WINDOW_STATE_BELOW) != 0;

  child = gtk_bin_get_child (&(window->bin));
  if (child)
    gtk_widget_unmap (child);
}

/* (Note: Replace "size" with "width" or "height". Also, the request
 * mode is honoured.)
 * For selecting the default window size, the following conditions
 * should hold (in order of importance):
 * - the size is not below the minimum size
 *   Windows cannot be resized below their minimum size, so we must
 *   ensure we don't do that either.
 * - the size is not above the natural size
 *   It seems weird to allocate more than this in an initial guess.
 * - the size does not exceed that of a maximized window
 *   We want to see the whole window after all.
 *   (Note that this may not be possible to achieve due to imperfect
 *    information from the windowing system.)
 */

/* We use these for now to not make windows too big by accident. Note
 * that we still clamp these numbers by screen size. Also note that
 * minimum size still overrides this. So keep your windows small! :)
 */
#define MAX_DEFAULT_WINDOW_WIDTH 640
#define MAX_DEFAULT_WINDOW_HEIGHT 480

static void
gtk_window_guess_default_size (GtkWindow *window,
                               gint      *width,
                               gint      *height)
{
  GtkWidget *widget = GTK_WIDGET (window);
  GdkScreen *screen;
  int minimum, natural;

  screen = gtk_widget_get_screen (widget);

  *width = gdk_screen_get_width (screen);
  *height = gdk_screen_get_height (screen);

  if (*width >= *height)
    {
      /* landscape */
      *width = MIN (*width, MAX_DEFAULT_WINDOW_WIDTH);
      *height = MIN (*height, MAX_DEFAULT_WINDOW_HEIGHT);
    }
  else
    {
      /* portrait */
      *width = MIN (*width, MAX_DEFAULT_WINDOW_HEIGHT);
      *height = MIN (*height, MAX_DEFAULT_WINDOW_WIDTH);
    }

  if (gtk_widget_get_request_mode (widget) == GTK_SIZE_REQUEST_WIDTH_FOR_HEIGHT)
    {
      gtk_widget_get_preferred_height (widget, &minimum, &natural);
      *height = MAX (minimum, MIN (*height, natural));

      gtk_widget_get_preferred_width_for_height (widget, *height, &minimum, &natural);
      *width = MAX (minimum, MIN (*width, natural));
    }
  else /* GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH or CONSTANT_SIZE */
    {
      gtk_widget_get_preferred_width (widget, &minimum, &natural);
      *width = MAX (minimum, MIN (*width, natural));

      gtk_widget_get_preferred_height_for_width (widget, *width, &minimum, &natural);
      *height = MAX (minimum, MIN (*height, natural));
    }
}

static void
gtk_window_realize (GtkWidget *widget)
{
  GtkAllocation allocation;
  GtkWindow *window;
  GdkWindow *parent_window;
  GdkWindow *gdk_window;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GtkWindowPrivate *priv;
  GtkStyleContext *context;

  window = GTK_WINDOW (widget);
  priv = window->priv;

  gtk_widget_get_allocation (widget, &allocation);

  if (gtk_widget_get_parent_window (widget))
    {
      gtk_container_set_resize_mode (GTK_CONTAINER (widget), GTK_RESIZE_PARENT);

      gtk_widget_set_realized (widget, TRUE);

      attributes.x = allocation.x;
      attributes.y = allocation.y;
      attributes.width = allocation.width;
      attributes.height = allocation.height;
      attributes.window_type = GDK_WINDOW_CHILD;

      attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK;

      attributes.visual = gtk_widget_get_visual (widget);
      attributes.wclass = GDK_INPUT_OUTPUT;

      attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

      gdk_window = gdk_window_new (gtk_widget_get_parent_window (widget),
				   &attributes, attributes_mask);
      gtk_widget_set_window (widget, gdk_window);
      gdk_window_set_user_data (gdk_window, widget);

      gtk_style_context_set_background (gtk_widget_get_style_context (widget), gdk_window);

      gdk_window_enable_synchronized_configure (gdk_window);
      return;
    }

  gtk_container_set_resize_mode (GTK_CONTAINER (window), GTK_RESIZE_QUEUE);

  /* ensure widget tree is properly size allocated */
  if (allocation.x == -1 &&
      allocation.y == -1 &&
      allocation.width == 1 &&
      allocation.height == 1)
    {

      allocation.x = 0;
      allocation.y = 0;

      gtk_window_guess_default_size (window, &allocation.width, &allocation.height);
      if (allocation.width == 0 || allocation.height == 0)
	{
	  /* non-empty window */
	  allocation.width = 200;
	  allocation.height = 200;
	}
      gtk_widget_size_allocate (widget, &allocation);
      
      _gtk_container_queue_resize (GTK_CONTAINER (widget));

      g_return_if_fail (!gtk_widget_get_realized (widget));
    }
  
  gtk_widget_set_realized (widget, TRUE);
  
  switch (priv->type)
    {
    case GTK_WINDOW_TOPLEVEL:
      attributes.window_type = GDK_WINDOW_TOPLEVEL;
      break;
    case GTK_WINDOW_POPUP:
      attributes.window_type = GDK_WINDOW_TEMP;
      break;
    default:
      g_warning (G_STRLOC": Unknown window type %d!", priv->type);
      break;
    }

  attributes.title = priv->title;
  attributes.wmclass_name = priv->wmclass_name;
  attributes.wmclass_class = priv->wmclass_class;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);

  attributes_mask = 0;
  parent_window = gtk_widget_get_root_window (widget);

  gtk_widget_get_allocation (widget, &allocation);
  attributes.width = allocation.width;
  attributes.height = allocation.height;
  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= (GDK_EXPOSURE_MASK |
			    GDK_KEY_PRESS_MASK |
			    GDK_KEY_RELEASE_MASK |
			    GDK_ENTER_NOTIFY_MASK |
			    GDK_LEAVE_NOTIFY_MASK |
			    GDK_FOCUS_CHANGE_MASK |
			    GDK_STRUCTURE_MASK);
  attributes.type_hint = priv->type_hint;

  attributes_mask |= GDK_WA_VISUAL | GDK_WA_TYPE_HINT;
  attributes_mask |= (priv->title ? GDK_WA_TITLE : 0);
  attributes_mask |= (priv->wmclass_name ? GDK_WA_WMCLASS : 0);

  gdk_window = gdk_window_new (parent_window, &attributes, attributes_mask);
  gtk_widget_set_window (widget, gdk_window);

  if (priv->opacity_set)
    gdk_window_set_opacity (gdk_window, priv->opacity);

  gdk_window_enable_synchronized_configure (gdk_window);

  gdk_window_set_user_data (gdk_window, window);

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_set_background (context, gdk_window);


  if (priv->transient_parent &&
      gtk_widget_get_realized (GTK_WIDGET (priv->transient_parent)))
    gdk_window_set_transient_for (gdk_window,
                                  gtk_widget_get_window (GTK_WIDGET (priv->transient_parent)));

  if (priv->wm_role)
    gdk_window_set_role (gdk_window, priv->wm_role);
  
  if (!priv->decorated)
    gdk_window_set_decorations (gdk_window, 0);
  
  if (!priv->deletable)
    gdk_window_set_functions (gdk_window, GDK_FUNC_ALL | GDK_FUNC_CLOSE);
  
  if (gtk_window_get_skip_pager_hint (window))
    gdk_window_set_skip_pager_hint (gdk_window, TRUE);
  
  if (gtk_window_get_skip_taskbar_hint (window))
    gdk_window_set_skip_taskbar_hint (gdk_window, TRUE);
  
  if (gtk_window_get_accept_focus (window))
    gdk_window_set_accept_focus (gdk_window, TRUE);
  else
    gdk_window_set_accept_focus (gdk_window, FALSE);
  
  if (gtk_window_get_focus_on_map (window))
    gdk_window_set_focus_on_map (gdk_window, TRUE);
  else
    gdk_window_set_focus_on_map (gdk_window, FALSE);
  
  if (priv->modal)
    gdk_window_set_modal_hint (gdk_window, TRUE);
  else
    gdk_window_set_modal_hint (gdk_window, FALSE);

  if (priv->startup_id)
    {
#ifdef GDK_WINDOWING_X11
      if (GDK_IS_X11_WINDOW (gdk_window))
        {
          guint32 timestamp = extract_time_from_startup_id (priv->startup_id);
          if (timestamp != GDK_CURRENT_TIME)
            gdk_x11_window_set_user_time (gdk_window, timestamp);
        }
#endif
      if (!startup_id_is_fake (priv->startup_id))
        gdk_window_set_startup_id (gdk_window, priv->startup_id);
    }

#ifdef GDK_WINDOWING_X11
  if (priv->initial_timestamp != GDK_CURRENT_TIME)
    {
      if (GDK_IS_X11_WINDOW (gdk_window))
        gdk_x11_window_set_user_time (gdk_window, priv->initial_timestamp);
    }
#endif

  /* Icons */
  gtk_window_realize_icon (window);
  
  if (priv->has_resize_grip)
    resize_grip_create_window (window);
}

static void
gtk_window_unrealize (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  GtkWindowGeometryInfo *info;

  /* On unrealize, we reset the size of the window such
   * that we will re-apply the default sizing stuff
   * next time we show the window.
   *
   * Default positioning is reset on unmap, instead of unrealize.
   */
  priv->need_default_size = TRUE;
  info = gtk_window_get_geometry_info (window, FALSE);
  if (info)
    {
      info->resize_width = -1;
      info->resize_height = -1;
      info->last.configure_request.x = 0;
      info->last.configure_request.y = 0;
      info->last.configure_request.width = -1;
      info->last.configure_request.height = -1;
      /* be sure we reset geom hints on re-realize */
      info->last.flags = 0;
    }

  /* Icons */
  gtk_window_unrealize_icon (window);

  if (priv->grip_window != NULL)
    resize_grip_destroy_window (window);

  GTK_WIDGET_CLASS (gtk_window_parent_class)->unrealize (widget);
}

static GtkJunctionSides
get_grip_junction (GtkWidget *widget)
{
  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    return GTK_JUNCTION_CORNER_BOTTOMRIGHT;
  else
    return GTK_JUNCTION_CORNER_BOTTOMLEFT;
}

static gboolean
get_drag_edge (GtkWidget     *widget,
               GdkWindowEdge *edge)
{
  GtkWindowPrivate *priv = GTK_WINDOW (widget)->priv;
  gboolean hresizable;
  gboolean vresizable;
  GtkTextDirection dir;
  GtkWindowGeometryInfo *info;

  hresizable = TRUE;
  vresizable = TRUE;

  info = priv->geometry_info;
  if (info)
    {
      GdkWindowHints flags = info->last.flags;
      GdkGeometry *geometry = &info->last.geometry;

      if ((flags & GDK_HINT_MIN_SIZE) && (flags & GDK_HINT_MAX_SIZE))
        {
          hresizable = geometry->min_width < geometry->max_width;
          vresizable = geometry->min_height < geometry->max_height;
        }
    }

  dir = gtk_widget_get_direction (widget);

  if (hresizable && vresizable)
    *edge = dir == GTK_TEXT_DIR_LTR ? GDK_WINDOW_EDGE_SOUTH_EAST : GDK_WINDOW_EDGE_SOUTH_WEST;
  else if (hresizable)
    *edge = dir == GTK_TEXT_DIR_LTR ? GDK_WINDOW_EDGE_EAST : GDK_WINDOW_EDGE_WEST;
  else if (vresizable)
    *edge = GDK_WINDOW_EDGE_SOUTH;
  else
    return FALSE;

  return TRUE;
}

static void
set_grip_cursor (GtkWindow *window)
{
  GtkWidget *widget = GTK_WIDGET (window);
  GtkWindowPrivate *priv = window->priv;

  if (priv->grip_window == NULL)
    return;

  if (gtk_widget_is_sensitive (widget))
    {
      GdkWindowEdge edge;
      GdkDisplay *display;
      GdkCursorType cursor_type;
      GdkCursor *cursor;

      cursor_type = GDK_LEFT_PTR;

      if (get_drag_edge (widget, &edge))
        {
          switch (edge)
            {
            case GDK_WINDOW_EDGE_EAST:
              cursor_type = GDK_RIGHT_SIDE;
              break;
            case GDK_WINDOW_EDGE_SOUTH_EAST:
              cursor_type = GDK_BOTTOM_RIGHT_CORNER;
              break;
            case GDK_WINDOW_EDGE_SOUTH:
              cursor_type = GDK_BOTTOM_SIDE;
              break;
            case GDK_WINDOW_EDGE_SOUTH_WEST:
              cursor_type = GDK_BOTTOM_LEFT_CORNER;
              break;
            case GDK_WINDOW_EDGE_WEST:
              cursor_type = GDK_LEFT_SIDE;
              break;
            default: ;
            }
        }

      display = gtk_widget_get_display (widget);
      cursor = gdk_cursor_new_for_display (display, cursor_type);
      gdk_window_set_cursor (priv->grip_window, cursor);
      g_object_unref (cursor);
    }
  else
    gdk_window_set_cursor (priv->grip_window, NULL);
}

static void
set_grip_shape (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  cairo_region_t *region;
  cairo_surface_t *surface;
  cairo_t *cr;
  double width, height;

  if (priv->grip_window == NULL)
    return;

  width = gdk_window_get_width (priv->grip_window);
  height = gdk_window_get_height (priv->grip_window);
  surface = cairo_image_surface_create (CAIRO_FORMAT_A1, width, height);

  cr = cairo_create (surface);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint (cr);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
  if (get_grip_junction (GTK_WIDGET (window)) & GTK_JUNCTION_CORNER_BOTTOMRIGHT)
    {
      cairo_move_to (cr, width, 0.0);
      cairo_line_to (cr, width, height);
      cairo_line_to (cr, 0.0, height);
    }
  else
    {
      cairo_move_to (cr, 0.0, 0.0);
      cairo_line_to (cr, width, height);
      cairo_line_to (cr, 0.0, height);
    }
  cairo_close_path (cr);
  cairo_fill (cr);
  cairo_destroy (cr);
  region = gdk_cairo_region_create_from_surface (surface);
  cairo_surface_destroy (surface);

  gdk_window_shape_combine_region (priv->grip_window, region, 0, 0);
  cairo_region_destroy (region);
}

static void
set_grip_position (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  GdkRectangle rect;

  if (priv->grip_window == NULL)
    return;

  gtk_window_get_resize_grip_area (window, &rect);
  gdk_window_raise (priv->grip_window);
  gdk_window_move_resize (priv->grip_window,
                          rect.x, rect.y,
                          rect.width, rect.height);
}

/* _gtk_window_set_allocation:
 * @window: a #GtkWindow
 * @allocation: the new allocation
 *
 * This function is like gtk_widget_set_allocation()
 * but does the necessary extra work to update
 * the resize grip positioning, etc.
 *
 * Call this instead of gtk_widget_set_allocation()
 * when overriding ::size_allocate in a GtkWindow
 * subclass without chaining up.
 */
void
_gtk_window_set_allocation (GtkWindow     *window,
                            GtkAllocation *allocation)
{
  GtkWidget *widget = (GtkWidget *)window;

  gtk_widget_set_allocation (widget, allocation);

  if (gtk_widget_get_realized (widget))
    {
      /* If it's not a toplevel we're embedded, we need to resize
       * the window's window and skip the grip.
       */
      if (!gtk_widget_is_toplevel (widget))
        {
          gdk_window_move_resize (gtk_widget_get_window (widget),
                                  allocation->x, allocation->y,
                                  allocation->width, allocation->height);
        }
      else
        {
          update_grip_visibility (window);
          set_grip_position (window);
        }
    }
}

static void
gtk_window_size_allocate (GtkWidget     *widget,
                          GtkAllocation *allocation)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkAllocation child_allocation;
  GtkWidget *child;
  guint border_width;

  _gtk_window_set_allocation (window, allocation);

  child = gtk_bin_get_child (&(window->bin));
  if (child && gtk_widget_get_visible (child))
    {
      border_width = gtk_container_get_border_width (GTK_CONTAINER (window));
      child_allocation.x = border_width;
      child_allocation.y = border_width;
      child_allocation.width = MAX (1, allocation->width - border_width * 2);
      child_allocation.height = MAX (1, allocation->height - border_width * 2);

      gtk_widget_size_allocate (child, &child_allocation);
    }
}

static gint
gtk_window_configure_event (GtkWidget         *widget,
			    GdkEventConfigure *event)
{
  GtkAllocation allocation;
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  gboolean expected_reply = priv->configure_request_count > 0;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    {
      if (GTK_WIDGET_CLASS (gtk_window_parent_class)->configure_event)
  	return GTK_WIDGET_CLASS (gtk_window_parent_class)->configure_event (widget, event);

      gdk_window_configure_finished (gtk_widget_get_window (widget));
      return FALSE;
    }

  /* priv->configure_request_count incremented for each
   * configure request, and decremented to a min of 0 for
   * each configure notify.
   *
   * All it means is that we know we will get at least
   * priv->configure_request_count more configure notifies.
   * We could get more configure notifies than that; some
   * of the configure notifies we get may be unrelated to
   * the configure requests. But we will get at least
   * priv->configure_request_count notifies.
   */

  if (priv->configure_request_count > 0)
    {
      priv->configure_request_count -= 1;
      gdk_window_thaw_toplevel_updates_libgtk_only (gtk_widget_get_window (widget));
    }
  
  /* As an optimization, we avoid a resize when possible.
   *
   * The only times we can avoid a resize are:
   *   - we know only the position changed, not the size
   *   - we know we have made more requests and so will get more
   *     notifies and can wait to resize when we get them
   */
  gtk_widget_get_allocation (widget, &allocation);
  if (!expected_reply &&
      (allocation.width == event->width &&
       allocation.height == event->height))
    {
      gdk_window_configure_finished (gtk_widget_get_window (widget));
      return TRUE;
    }

  /*
   * If we do need to resize, we do that by:
   *   - filling in widget->allocation with the new size
   *   - setting configure_notify_received to TRUE
   *     for use in gtk_window_move_resize()
   *   - queueing a resize, leading to invocation of
   *     gtk_window_move_resize() in an idle handler
   *
   */
  
  priv->configure_notify_received = TRUE;

  allocation.width = event->width;
  allocation.height = event->height;
  gtk_widget_set_allocation (widget, &allocation);

  gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE); // XXX - What was this for again?

  _gtk_container_queue_resize (GTK_CONTAINER (widget));
  
  return TRUE;
}

static gboolean
gtk_window_state_event (GtkWidget           *widget,
                        GdkEventWindowState *event)
{
  update_grip_visibility (GTK_WINDOW (widget));

  if (event->changed_mask & GDK_WINDOW_STATE_FOCUSED)
    ensure_state_flag_backdrop (widget);

  return FALSE;
}

static void
gtk_window_direction_changed (GtkWidget        *widget,
                              GtkTextDirection  prev_dir)
{
  GtkWindow *window = GTK_WINDOW (widget);

  set_grip_cursor (window);
  set_grip_position (window);
  set_grip_shape (window);
}

static void
gtk_window_state_changed (GtkWidget    *widget,
                          GtkStateType  previous_state)
{
  GtkWindow *window = GTK_WINDOW (widget);

  update_grip_visibility (window);
}

static void
gtk_window_style_updated (GtkWidget *widget)
{
  GtkWindow *window = GTK_WINDOW (widget);
  GtkWindowPrivate *priv = window->priv;
  GdkRectangle rect;

  GTK_WIDGET_CLASS (gtk_window_parent_class)->style_updated (widget);

  if (priv->grip_window != NULL && gtk_window_get_resize_grip_area (window, &rect))
    {
      gdk_window_move_resize (priv->grip_window,
                              rect.x, rect.y,
                              rect.width, rect.height);

      set_grip_shape (window);
    }
}

static void
resize_grip_create_window (GtkWindow *window)
{
  GtkWidget *widget;
  GtkWindowPrivate *priv;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GdkRectangle rect;
  GdkRGBA transparent = {0, 0, 0, 0};

  priv = window->priv;
  widget = GTK_WIDGET (window);

  g_return_if_fail (gtk_widget_get_realized (widget));
  g_return_if_fail (priv->grip_window == NULL);

  gtk_window_get_resize_grip_area (window, &rect);

  attributes.x = rect.x;
  attributes.y = rect.y;
  attributes.width = rect.width;
  attributes.height = rect.height;
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.event_mask = gtk_widget_get_events (widget) |
                          GDK_EXPOSURE_MASK |
                          GDK_BUTTON_PRESS_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y;

  priv->grip_window = gdk_window_new (gtk_widget_get_window (widget),
                                      &attributes,
                                      attributes_mask);
  gdk_window_set_background_rgba (priv->grip_window, &transparent);

  gdk_window_set_user_data (priv->grip_window, widget);

  gdk_window_raise (priv->grip_window);

  set_grip_shape (window);
  update_grip_visibility (window);
}

static void
resize_grip_destroy_window (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  gdk_window_set_user_data (priv->grip_window, NULL);
  gdk_window_destroy (priv->grip_window);
  priv->grip_window = NULL;
  update_grip_visibility (window);
}

/**
 * gtk_window_set_has_resize_grip:
 * @window: a #GtkWindow
 * @value: %TRUE to allow a resize grip
 *
 * Sets whether @window has a corner resize grip.
 *
 * Note that the resize grip is only shown if the window
 * is actually resizable and not maximized. Use
 * gtk_window_resize_grip_is_visible() to find out if the
 * resize grip is currently shown.
 *
 * Since: 3.0
 */
void
gtk_window_set_has_resize_grip (GtkWindow *window,
                                gboolean   value)
{
  GtkWidget *widget = GTK_WIDGET (window);
  GtkWindowPrivate *priv = window->priv;

  value = value != FALSE;

  if (value != priv->has_resize_grip)
    {
      priv->has_resize_grip = value;
      gtk_widget_queue_draw (widget);

      if (gtk_widget_get_realized (widget) && 
	  gtk_widget_is_toplevel (GTK_WIDGET (widget)))
        {
          if (priv->has_resize_grip && priv->grip_window == NULL)
            resize_grip_create_window (window);
          else if (!priv->has_resize_grip && priv->grip_window != NULL)
            resize_grip_destroy_window (window);
        }

      g_object_notify (G_OBJECT (window), "has-resize-grip");
    }
}

static void
update_grip_visibility (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  gboolean val;

  val = gtk_window_resize_grip_is_visible (window);

  if (priv->grip_window != NULL)
    {
      if (val)
        {
          gdk_window_show (priv->grip_window);
          set_grip_cursor (window);
        }
      else
        {
          gdk_window_hide (priv->grip_window);
        }
    }

  if (priv->resize_grip_visible != val)
    {
      priv->resize_grip_visible = val;

      g_object_notify (G_OBJECT (window), "resize-grip-visible");
    }
}

/**
 * gtk_window_resize_grip_is_visible:
 * @window: a #GtkWindow
 *
 * Determines whether a resize grip is visible for the specified window.
 *
 * Returns: %TRUE if a resize grip exists and is visible
 *
 * Since: 3.0
 */
gboolean
gtk_window_resize_grip_is_visible (GtkWindow *window)
{
  GtkWidget *widget;
  GtkWindowPrivate *priv;
  GdkWindowEdge edge;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;
  widget = GTK_WIDGET (window);

  if (priv->type == GTK_WINDOW_POPUP)
    return FALSE;

  if (!priv->resizable)
    return FALSE;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    return FALSE;

  if (gtk_widget_get_realized (widget))
    {
      GdkWindowState state;

      state = gdk_window_get_state (gtk_widget_get_window (widget));

      if (state & GDK_WINDOW_STATE_MAXIMIZED || state & GDK_WINDOW_STATE_FULLSCREEN)
        return FALSE;
    }

  if (!get_drag_edge (widget, &edge))
    return FALSE;

  return window->priv->has_resize_grip;
}

/**
 * gtk_window_get_has_resize_grip:
 * @window: a #GtkWindow
 *
 * Determines whether the window may have a resize grip.
 *
 * Returns: %TRUE if the window has a resize grip
 *
 * Since: 3.0
 */
gboolean
gtk_window_get_has_resize_grip (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->has_resize_grip;
}

/**
 * gtk_window_get_resize_grip_area:
 * @window: a #GtkWindow
 * @rect: (out): a pointer to a #GdkRectangle which we should store
 *     the resize grip area
 *
 * If a window has a resize grip, this will retrieve the grip
 * position, width and height into the specified #GdkRectangle.
 *
 * Returns: %TRUE if the resize grip's area was retrieved
 *
 * Since: 3.0
 */
gboolean
gtk_window_get_resize_grip_area (GtkWindow    *window,
                                 GdkRectangle *rect)
{
  GtkWidget *widget = GTK_WIDGET (window);
  GtkAllocation allocation;
  gint grip_width;
  gint grip_height;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  if (!window->priv->has_resize_grip)
    return FALSE;

  gtk_widget_get_allocation (widget, &allocation);

  gtk_widget_style_get (widget,
                        "resize-grip-width", &grip_width,
                        "resize-grip-height", &grip_height,
                        NULL);

  if (grip_width > allocation.width)
    grip_width = allocation.width;

  if (grip_height > allocation.height)
    grip_height = allocation.height;

  rect->width = grip_width;
  rect->height = grip_height;
  rect->y = allocation.y + allocation.height - grip_height;

  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    rect->x = allocation.x + allocation.width - grip_width;
  else
    rect->x = allocation.x;

  return TRUE;
}

/* the accel_key and accel_mods fields of the key have to be setup
 * upon calling this function. it'll then return whether that key
 * is at all used as accelerator, and if so will OR in the
 * accel_flags member of the key.
 */
gboolean
_gtk_window_query_nonaccels (GtkWindow      *window,
			     guint           accel_key,
			     GdkModifierType accel_mods)
{
  GtkWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;

  /* movement keys are considered locked accels */
  if (!accel_mods)
    {
      static const guint bindings[] = {
	GDK_KEY_space, GDK_KEY_KP_Space, GDK_KEY_Return, GDK_KEY_ISO_Enter, GDK_KEY_KP_Enter, GDK_KEY_Up, GDK_KEY_KP_Up, GDK_KEY_Down, GDK_KEY_KP_Down,
	GDK_KEY_Left, GDK_KEY_KP_Left, GDK_KEY_Right, GDK_KEY_KP_Right, GDK_KEY_Tab, GDK_KEY_KP_Tab, GDK_KEY_ISO_Left_Tab,
      };
      guint i;
      
      for (i = 0; i < G_N_ELEMENTS (bindings); i++)
	if (bindings[i] == accel_key)
	  return TRUE;
    }

  /* mnemonics are considered locked accels */
  if (accel_mods == priv->mnemonic_modifier)
    {
      GtkMnemonicHash *mnemonic_hash = gtk_window_get_mnemonic_hash (window, FALSE);
      if (mnemonic_hash && _gtk_mnemonic_hash_lookup (mnemonic_hash, accel_key))
	return TRUE;
    }

  return FALSE;
}

/**
 * gtk_window_propagate_key_event:
 * @window:  a #GtkWindow
 * @event:   a #GdkEventKey
 *
 * Propagate a key press or release event to the focus widget and
 * up the focus container chain until a widget handles @event.
 * This is normally called by the default ::key_press_event and
 * ::key_release_event handlers for toplevel windows,
 * however in some cases it may be useful to call this directly when
 * overriding the standard key handling for a toplevel window.
 *
 * Return value: %TRUE if a widget in the focus chain handled the event.
 *
 * Since: 2.4
 */
gboolean
gtk_window_propagate_key_event (GtkWindow        *window,
                                GdkEventKey      *event)
{
  GtkWindowPrivate *priv;
  gboolean handled = FALSE;
  GtkWidget *widget, *focus;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  priv = window->priv;
  widget = GTK_WIDGET (window);

  focus = priv->focus_widget;
  if (focus)
    g_object_ref (focus);
  
  while (!handled &&
         focus && focus != widget &&
         gtk_widget_get_toplevel (focus) == widget)
    {
      GtkWidget *parent;
      
      if (gtk_widget_is_sensitive (focus))
        handled = gtk_widget_event (focus, (GdkEvent*) event);

      parent = gtk_widget_get_parent (focus);
      if (parent)
        g_object_ref (parent);
      
      g_object_unref (focus);
      
      focus = parent;
    }
  
  if (focus)
    g_object_unref (focus);

  return handled;
}

static gint
gtk_window_key_press_event (GtkWidget   *widget,
			    GdkEventKey *event)
{
  GtkWindow *window = GTK_WINDOW (widget);
  gboolean handled = FALSE;

  /* handle mnemonics and accelerators */
  if (!handled)
    handled = gtk_window_activate_key (window, event);

  /* handle focus widget key events */
  if (!handled)
    handled = gtk_window_propagate_key_event (window, event);

  /* Chain up, invokes binding set */
  if (!handled)
    handled = GTK_WIDGET_CLASS (gtk_window_parent_class)->key_press_event (widget, event);

  return handled;
}

static gint
gtk_window_key_release_event (GtkWidget   *widget,
			      GdkEventKey *event)
{
  GtkWindow *window = GTK_WINDOW (widget);
  gboolean handled = FALSE;

  /* handle focus widget key events */
  if (!handled)
    handled = gtk_window_propagate_key_event (window, event);

  /* Chain up, invokes binding set */
  if (!handled)
    handled = GTK_WIDGET_CLASS (gtk_window_parent_class)->key_release_event (widget, event);

  return handled;
}

static gint
gtk_window_button_press_event (GtkWidget *widget,
                               GdkEventButton *event)
{
  GtkWindowPrivate *priv = GTK_WINDOW (widget)->priv;
  GdkWindowEdge edge;

  if (event->window == priv->grip_window)
    {
      if (get_drag_edge (widget, &edge))
        gdk_window_begin_resize_drag_for_device (gtk_widget_get_window (widget),
                                                 edge,
                                                 gdk_event_get_device ((GdkEvent *) event),
                                                 event->button,
                                                 event->x_root,
                                                 event->y_root,
                                                 event->time);

      return TRUE;
    }

  return FALSE;
}

static void
gtk_window_real_activate_default (GtkWindow *window)
{
  gtk_window_activate_default (window);
}

static void
gtk_window_real_activate_focus (GtkWindow *window)
{
  gtk_window_activate_focus (window);
}

static gint
gtk_window_enter_notify_event (GtkWidget        *widget,
			       GdkEventCrossing *event)
{
  return FALSE;
}

static gint
gtk_window_leave_notify_event (GtkWidget        *widget,
			       GdkEventCrossing *event)
{
  return FALSE;
}

static void
do_focus_change (GtkWidget *widget,
		 gboolean   in)
{
  GdkWindow *window;
  GdkDeviceManager *device_manager;
  GList *devices, *d;

  g_object_ref (widget);

  device_manager = gdk_display_get_device_manager (gtk_widget_get_display (widget));
  devices = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);
  devices = g_list_concat (devices, gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_SLAVE));
  devices = g_list_concat (devices, gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_FLOATING));

  for (d = devices; d; d = d->next)
    {
      GdkDevice *dev = d->data;
      GdkEvent *fevent;

      if (gdk_device_get_source (dev) != GDK_SOURCE_KEYBOARD)
        continue;

      /* Skip non-master keyboards that haven't
       * selected for events from this window
       */
      window = gtk_widget_get_window (widget);
      if (gdk_device_get_device_type (dev) != GDK_DEVICE_TYPE_MASTER &&
          window && !gdk_window_get_device_events (window, dev))
        continue;

      fevent = gdk_event_new (GDK_FOCUS_CHANGE);

      fevent->focus_change.type = GDK_FOCUS_CHANGE;
      fevent->focus_change.window = window;
      if (window)
        g_object_ref (window);
      fevent->focus_change.in = in;
      gdk_event_set_device (fevent, dev);

      gtk_widget_send_focus_change (widget, fevent);

      gdk_event_free (fevent);
    }

  g_list_free (devices);
  g_object_unref (widget);
}

static void
maybe_set_mnemonics_visible (GtkWindow *window)
{
  GList *devices, *d;
  GdkDeviceManager *device_manager;

  device_manager = gdk_display_get_device_manager (gtk_widget_get_display (GTK_WIDGET (window)));
  devices = gdk_device_manager_list_devices (device_manager, GDK_DEVICE_TYPE_MASTER);

  for (d = devices; d; d = d->next)
    {
      GdkDevice *dev = d->data;

      if (gdk_device_get_source (dev) == GDK_SOURCE_MOUSE)
        {
          GdkModifierType mask;

          gdk_device_get_state (dev, gtk_widget_get_window (GTK_WIDGET (window)),
                                NULL, &mask);
          if (window->priv->mnemonic_modifier == (mask & gtk_accelerator_get_default_mod_mask ()))
            {
              _gtk_window_set_auto_mnemonics_visible (window);
              break;
            }
        }
    }

  g_list_free (devices);
}

static gint
gtk_window_focus_in_event (GtkWidget     *widget,
			   GdkEventFocus *event)
{
  GtkWindow *window = GTK_WINDOW (widget);
  gboolean auto_mnemonics;

  /* It appears spurious focus in events can occur when
   *  the window is hidden. So we'll just check to see if
   *  the window is visible before actually handling the
   *  event
   */
  if (gtk_widget_get_visible (widget))
    {
      _gtk_window_set_has_toplevel_focus (window, TRUE);
      _gtk_window_set_is_active (window, TRUE);

      g_object_get (gtk_widget_get_settings (widget),
                    "gtk-auto-mnemonics", &auto_mnemonics, NULL);
      if (auto_mnemonics)
        maybe_set_mnemonics_visible (window);
    }

  return FALSE;
}

static gint
gtk_window_focus_out_event (GtkWidget     *widget,
			    GdkEventFocus *event)
{
  GtkWindow *window = GTK_WINDOW (widget);
  gboolean auto_mnemonics;

  _gtk_window_set_has_toplevel_focus (window, FALSE);
  _gtk_window_set_is_active (window, FALSE);

  /* set the mnemonic-visible property to false */
  g_object_get (gtk_widget_get_settings (widget),
                "gtk-auto-mnemonics", &auto_mnemonics, NULL);
  if (auto_mnemonics)
    gtk_window_set_mnemonics_visible (window, FALSE);

  return FALSE;
}

static void
gtk_window_check_resize (GtkContainer *container)
{
  /* If the window is not toplevel anymore than it's embedded somewhere,
   * so handle it like a normal window */
  if (!gtk_widget_is_toplevel (GTK_WIDGET (container)))
    GTK_CONTAINER_CLASS (gtk_window_parent_class)->check_resize (container);
  else if (gtk_widget_get_visible (GTK_WIDGET (container)))
    gtk_window_move_resize (GTK_WINDOW (container));
}

static gboolean
gtk_window_focus (GtkWidget        *widget,
		  GtkDirectionType  direction)
{
  GtkWindowPrivate *priv;
  GtkBin *bin;
  GtkWindow *window;
  GtkContainer *container;
  GtkWidget *child;
  GtkWidget *old_focus_child;
  GtkWidget *parent;

  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    return GTK_WIDGET_CLASS (gtk_window_parent_class)->focus (widget, direction);

  container = GTK_CONTAINER (widget);
  window = GTK_WINDOW (widget);
  priv = window->priv;
  bin = GTK_BIN (widget);

  old_focus_child = gtk_container_get_focus_child (container);
  
  /* We need a special implementation here to deal properly with wrapping
   * around in the tab chain without the danger of going into an
   * infinite loop.
   */
  if (old_focus_child)
    {
      if (gtk_widget_child_focus (old_focus_child, direction))
	return TRUE;
    }

  if (priv->focus_widget)
    {
      if (direction == GTK_DIR_LEFT ||
	  direction == GTK_DIR_RIGHT ||
	  direction == GTK_DIR_UP ||
	  direction == GTK_DIR_DOWN)
	{
	  return FALSE;
	}
      
      /* Wrapped off the end, clear the focus setting for the toplpevel */
      parent = gtk_widget_get_parent (priv->focus_widget);
      while (parent)
	{
	  gtk_container_set_focus_child (GTK_CONTAINER (parent), NULL);
	  parent = gtk_widget_get_parent (parent);
	}
      
      gtk_window_set_focus (GTK_WINDOW (container), NULL);
    }

  /* Now try to focus the first widget in the window */
  child = gtk_bin_get_child (bin);
  if (child)
    {
      if (gtk_widget_child_focus (child, direction))
        return TRUE;
    }

  return FALSE;
}

static void
gtk_window_move_focus (GtkWidget       *widget,
                       GtkDirectionType dir)
{
  if (!gtk_widget_is_toplevel (GTK_WIDGET (widget)))
    {
      GTK_WIDGET_CLASS (gtk_window_parent_class)->move_focus (widget, dir);
      return;
    }

  gtk_widget_child_focus (widget, dir);

  if (! gtk_container_get_focus_child (GTK_CONTAINER (widget)))
    gtk_window_set_focus (GTK_WINDOW (widget), NULL);
}

static void
gtk_window_real_set_focus (GtkWindow *window,
			   GtkWidget *focus)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *old_focus = priv->focus_widget;
  gboolean had_default = FALSE;
  gboolean focus_had_default = FALSE;
  gboolean old_focus_had_default = FALSE;

  if (old_focus)
    {
      g_object_ref (old_focus);
      g_object_freeze_notify (G_OBJECT (old_focus));
      old_focus_had_default = gtk_widget_has_default (old_focus);
    }
  if (focus)
    {
      g_object_ref (focus);
      g_object_freeze_notify (G_OBJECT (focus));
      focus_had_default = gtk_widget_has_default (focus);
    }

  if (priv->default_widget)
    had_default = gtk_widget_has_default (priv->default_widget);

  if (priv->focus_widget)
    {
      if (gtk_widget_get_receives_default (priv->focus_widget) &&
	  (priv->focus_widget != priv->default_widget))
        {
          _gtk_widget_set_has_default (priv->focus_widget, FALSE);
	  gtk_widget_queue_draw (priv->focus_widget);

	  if (priv->default_widget)
            _gtk_widget_set_has_default (priv->default_widget, TRUE);
	}

      priv->focus_widget = NULL;

      if (priv->has_focus)
	do_focus_change (old_focus, FALSE);

      g_object_notify (G_OBJECT (old_focus), "is-focus");
    }

  /* The above notifications may have set a new focus widget,
   * if so, we don't want to override it.
   */
  if (focus && !priv->focus_widget)
    {
      priv->focus_widget = focus;

      if (gtk_widget_get_receives_default (priv->focus_widget) &&
	  (priv->focus_widget != priv->default_widget))
	{
	  if (gtk_widget_get_can_default (priv->focus_widget))
            _gtk_widget_set_has_default (priv->focus_widget, TRUE);

	  if (priv->default_widget)
            _gtk_widget_set_has_default (priv->default_widget, FALSE);
	}

      if (priv->has_focus)
	do_focus_change (priv->focus_widget, TRUE);

      g_object_notify (G_OBJECT (priv->focus_widget), "is-focus");
    }

  /* If the default widget changed, a redraw will have been queued
   * on the old and new default widgets by gtk_window_set_default(), so
   * we only have to worry about the case where it didn't change.
   * We'll sometimes queue a draw twice on the new widget but that
   * is harmless.
   */
  if (priv->default_widget &&
      (had_default != gtk_widget_has_default (priv->default_widget)))
    gtk_widget_queue_draw (priv->default_widget);
  
  if (old_focus)
    {
      if (old_focus_had_default != gtk_widget_has_default (old_focus))
	gtk_widget_queue_draw (old_focus);
	
      g_object_thaw_notify (G_OBJECT (old_focus));
      g_object_unref (old_focus);
    }
  if (focus)
    {
      if (focus_had_default != gtk_widget_has_default (focus))
	gtk_widget_queue_draw (focus);

      g_object_thaw_notify (G_OBJECT (focus));
      g_object_unref (focus);
    }
}

/**
 * _gtk_window_unset_focus_and_default:
 * @window: a #GtkWindow
 * @widget: a widget inside of @window
 * 
 * Checks whether the focus and default widgets of @window are
 * @widget or a descendent of @widget, and if so, unset them.
 **/
void
_gtk_window_unset_focus_and_default (GtkWindow *window,
				     GtkWidget *widget)

{
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *child;
  GtkWidget *parent;

  g_object_ref (window);
  g_object_ref (widget);

  parent = gtk_widget_get_parent (widget);
  if (gtk_container_get_focus_child (GTK_CONTAINER (parent)) == widget)
    {
      child = priv->focus_widget;
      
      while (child && child != widget)
        child = gtk_widget_get_parent (child);

      if (child == widget)
	gtk_window_set_focus (GTK_WINDOW (window), NULL);
    }
      
  child = priv->default_widget;
      
  while (child && child != widget)
    child = gtk_widget_get_parent (child);

  if (child == widget)
    gtk_window_set_default (window, NULL);
  
  g_object_unref (widget);
  g_object_unref (window);
}

/*********************************
 * Functions related to resizing *
 *********************************/

static void
geometry_size_to_pixels (GdkGeometry *geometry,
			 guint        flags,
			 gint        *width,
			 gint        *height)
{
  gint base_width = 0;
  gint base_height = 0;
  gint min_width = 0;
  gint min_height = 0;
  gint width_inc = 1;
  gint height_inc = 1;

  if (flags & GDK_HINT_BASE_SIZE)
    {
      base_width = geometry->base_width;
      base_height = geometry->base_height;
    }
  if (flags & GDK_HINT_MIN_SIZE)
    {
      min_width = geometry->min_width;
      min_height = geometry->min_height;
    }
  if (flags & GDK_HINT_RESIZE_INC)
    {
      width_inc = geometry->width_inc;
      height_inc = geometry->height_inc;
    }

  if (width)
    *width = MAX (*width * width_inc + base_width, min_width);
  if (height)
    *height = MAX (*height * height_inc + base_height, min_height);
}

/* This function doesn't constrain to geometry hints */
static void 
gtk_window_compute_configure_request_size (GtkWindow   *window,
                                           GdkGeometry *geometry,
                                           guint        flags,
                                           gint        *width,
                                           gint        *height)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWindowGeometryInfo *info;

  /* Preconditions:
   *  - we've done a size request
   */
  
  info = gtk_window_get_geometry_info (window, FALSE);

  if (priv->need_default_size)
    {
      gtk_window_guess_default_size (window, width, height);

      /* If window is empty so requests 0, default to random nonzero size */
       if (*width == 0 && *height == 0)
         {
           *width = 200;
           *height = 200;
         }

       /* Override with default size */

       if (info)
	 {
	   if (info->default_width > 0)
	     *width = info->default_width;
	   if (info->default_height > 0)
	     *height = info->default_height;

	   if (info->default_is_geometry)
	     geometry_size_to_pixels (geometry, flags,
				      info->default_width > 0 ? width : NULL,
				      info->default_height > 0 ? height : NULL);
         }
    }
  else
    {
      GtkAllocation allocation;

      gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);

      /* Default to keeping current size */
      *width = allocation.width;
      *height = allocation.height;
    }

  /* Override any size with gtk_window_resize() values */
  if (info)
    {
      if (info->resize_width > 0)
	*width = info->resize_width;
      if (info->resize_height > 0)
	*height = info->resize_height;

      if (info->resize_is_geometry)
	geometry_size_to_pixels (geometry, flags,
				 info->resize_width > 0 ? width : NULL,
				 info->resize_height > 0 ? height : NULL);
    }

  /* Don't ever request zero width or height, its not supported by
     gdk. The size allocation code will round it to 1 anyway but if
     we do it then the value returned from this function will is
     not comparable to the size allocation read from the GtkWindow. */
  *width = MAX (*width, 1);
  *height = MAX (*height, 1);
}

static GtkWindowPosition
get_effective_position (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWindowPosition pos = priv->position;

  if (pos == GTK_WIN_POS_CENTER_ON_PARENT &&
      (priv->transient_parent == NULL ||
       !gtk_widget_get_mapped (GTK_WIDGET (priv->transient_parent))))
    pos = GTK_WIN_POS_NONE;

  return pos;
}

static int
get_center_monitor_of_window (GtkWindow *window)
{
  /* We could try to sort out the relative positions of the monitors and
   * stuff, or we could just be losers and assume you have a row
   * or column of monitors.
   */
  return gdk_screen_get_n_monitors (gtk_window_check_screen (window)) / 2;
}

static int
get_monitor_containing_pointer (GtkWindow *window)
{
  gint px, py;
  gint monitor_num;
  GdkScreen *window_screen;
  GdkScreen *pointer_screen;
  GdkDisplay *display;
  GdkDeviceManager *device_manager;
  GdkDevice *pointer;

  window_screen = gtk_window_check_screen (window);
  display = gdk_screen_get_display (window_screen);
  device_manager = gdk_display_get_device_manager (display);
  pointer = gdk_device_manager_get_client_pointer (device_manager);

  gdk_device_get_position (pointer,
                           &pointer_screen,
                           &px, &py);

  if (pointer_screen == window_screen)
    monitor_num = gdk_screen_get_monitor_at_point (pointer_screen, px, py);
  else
    monitor_num = -1;

  return monitor_num;
}

static void
center_window_on_monitor (GtkWindow *window,
                          gint       w,
                          gint       h,
                          gint      *x,
                          gint      *y)
{
  GdkRectangle monitor;
  int monitor_num;

  monitor_num = get_monitor_containing_pointer (window);

  if (monitor_num == -1)
    monitor_num = get_center_monitor_of_window (window);

  gdk_screen_get_monitor_workarea (gtk_window_check_screen (window),
                                    monitor_num, &monitor);

  *x = (monitor.width - w) / 2 + monitor.x;
  *y = (monitor.height - h) / 2 + monitor.y;

  /* Be sure we aren't off the monitor, ignoring _NET_WM_STRUT
   * and WM decorations.
   */
  if (*x < monitor.x)
    *x = monitor.x;
  if (*y < monitor.y)
    *y = monitor.y;
}

static void
clamp (gint *base,
       gint  extent,
       gint  clamp_base,
       gint  clamp_extent)
{
  if (extent > clamp_extent)
    /* Center */
    *base = clamp_base + clamp_extent/2 - extent/2;
  else if (*base < clamp_base)
    *base = clamp_base;
  else if (*base + extent > clamp_base + clamp_extent)
    *base = clamp_base + clamp_extent - extent;
}

static void
clamp_window_to_rectangle (gint               *x,
                           gint               *y,
                           gint                w,
                           gint                h,
                           const GdkRectangle *rect)
{
#ifdef DEBUGGING_OUTPUT
  g_print ("%s: %+d%+d %dx%d: %+d%+d: %dx%d", G_STRFUNC, rect->x, rect->y, rect->width, rect->height, *x, *y, w, h);
#endif

  /* If it is too large, center it. If it fits on the monitor but is
   * partially outside, move it to the closest edge. Do this
   * separately in x and y directions.
   */
  clamp (x, w, rect->x, rect->width);
  clamp (y, h, rect->y, rect->height);
#ifdef DEBUGGING_OUTPUT
  g_print (" ==> %+d%+d: %dx%d\n", *x, *y, w, h);
#endif
}


static void
gtk_window_compute_configure_request (GtkWindow    *window,
                                      GdkRectangle *request,
                                      GdkGeometry  *geometry,
                                      guint        *flags)
{
  GtkWindowPrivate *priv = window->priv;
  GdkGeometry new_geometry;
  guint new_flags;
  int w, h;
  GtkWindowPosition pos;
  GtkWidget *parent_widget;
  GtkWindowGeometryInfo *info;
  GdkScreen *screen;
  int x, y;

  screen = gtk_window_check_screen (window);

  gtk_window_compute_hints (window, &new_geometry, &new_flags);
  gtk_window_compute_configure_request_size (window,
                                             &new_geometry, new_flags,
                                             &w, &h);

  gtk_window_constrain_size (window,
                             &new_geometry, new_flags,
                             w, h,
                             &w, &h);

  parent_widget = (GtkWidget*) priv->transient_parent;

  pos = get_effective_position (window);
  info = gtk_window_get_geometry_info (window, FALSE);

  /* by default, don't change position requested */
  if (info)
    {
      x = info->last.configure_request.x;
      y = info->last.configure_request.y;
    }
  else
    {
      x = 0;
      y = 0;
    }


  if (priv->need_default_position)
    {

      /* FIXME this all interrelates with window gravity.
       * For most of them I think we want to set GRAVITY_CENTER.
       *
       * Not sure how to go about that.
       */
      switch (pos)
        {
          /* here we are only handling CENTER_ALWAYS
           * as it relates to default positioning,
           * where it's equivalent to simply CENTER
           */
        case GTK_WIN_POS_CENTER_ALWAYS:
        case GTK_WIN_POS_CENTER:
          center_window_on_monitor (window, w, h, &x, &y);
          break;

        case GTK_WIN_POS_CENTER_ON_PARENT:
          {
            GtkAllocation allocation;
            GdkWindow *gdk_window;
            gint monitor_num;
            GdkRectangle monitor;
            gint ox, oy;

            g_assert (gtk_widget_get_mapped (parent_widget)); /* established earlier */

            gdk_window = gtk_widget_get_window (parent_widget);

            if (gdk_window != NULL)
              monitor_num = gdk_screen_get_monitor_at_window (screen,
                                                              gdk_window);
            else
              monitor_num = -1;

            gdk_window_get_origin (gdk_window,
                                   &ox, &oy);

            gtk_widget_get_allocation (parent_widget, &allocation);
            x = ox + (allocation.width - w) / 2;
            y = oy + (allocation.height - h) / 2;

            /* Clamp onto current monitor, ignoring _NET_WM_STRUT and
             * WM decorations. If parent wasn't on a monitor, just
             * give up.
             */
            if (monitor_num >= 0)
              {
                gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);
                clamp_window_to_rectangle (&x, &y, w, h, &monitor);
              }
          }
          break;

        case GTK_WIN_POS_MOUSE:
          {
            gint screen_width = gdk_screen_get_width (screen);
            gint screen_height = gdk_screen_get_height (screen);
            gint monitor_num;
            GdkRectangle monitor;
            GdkDisplay *display;
            GdkDeviceManager *device_manager;
            GdkDevice *pointer;
            GdkScreen *pointer_screen;
            gint px, py;

            display = gdk_screen_get_display (screen);
            device_manager = gdk_display_get_device_manager (display);
            pointer = gdk_device_manager_get_client_pointer (device_manager);

            gdk_device_get_position (pointer,
                                     &pointer_screen,
                                     &px, &py);

            if (pointer_screen == screen)
              monitor_num = gdk_screen_get_monitor_at_point (screen, px, py);
            else
              monitor_num = -1;

            x = px - w / 2;
            y = py - h / 2;
            x = CLAMP (x, 0, screen_width - w);
            y = CLAMP (y, 0, screen_height - h);

            /* Clamp onto current monitor, ignoring _NET_WM_STRUT and
             * WM decorations. Don't try to figure out what's going
             * on if the mouse wasn't inside a monitor.
             */
            if (monitor_num >= 0)
              {
                gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);
                clamp_window_to_rectangle (&x, &y, w, h, &monitor);
              }
          }
          break;

        default:
          break;
        }
    } /* if (priv->need_default_position) */

  if (priv->need_default_position && info &&
      info->initial_pos_set)
    {
      x = info->initial_x;
      y = info->initial_y;
      gtk_window_constrain_position (window, w, h, &x, &y);
    }

  request->x = x;
  request->y = y;
  request->width = w;
  request->height = h;

  if (geometry)
    *geometry = new_geometry;
  if (flags)
    *flags = new_flags;
}

static void
gtk_window_constrain_position (GtkWindow    *window,
                               gint          new_width,
                               gint          new_height,
                               gint         *x,
                               gint         *y)
{
  GtkWindowPrivate *priv = window->priv;

  /* See long comments in gtk_window_move_resize()
   * on when it's safe to call this function.
   */
  if (priv->position == GTK_WIN_POS_CENTER_ALWAYS)
    {
      gint center_x, center_y;

      center_window_on_monitor (window, new_width, new_height, &center_x, &center_y);
      
      *x = center_x;
      *y = center_y;
    }
}

static void
gtk_window_move_resize (GtkWindow *window)
{
  /* Overview:
   *
   * First we determine whether any information has changed that would
   * cause us to revise our last configure request.  If we would send
   * a different configure request from last time, then
   * configure_request_size_changed = TRUE or
   * configure_request_pos_changed = TRUE. configure_request_size_changed
   * may be true due to new hints, a gtk_window_resize(), or whatever.
   * configure_request_pos_changed may be true due to gtk_window_set_position()
   * or gtk_window_move().
   *
   * If the configure request has changed, we send off a new one.  To
   * ensure GTK+ invariants are maintained (resize queue does what it
   * should), we go ahead and size_allocate the requested size in this
   * function.
   *
   * If the configure request has not changed, we don't ever resend
   * it, because it could mean fighting the user or window manager.
   *
   * 
   *   To prepare the configure request, we come up with a base size/pos:
   *      - the one from gtk_window_move()/gtk_window_resize()
   *      - else default_width, default_height if we haven't ever
   *        been mapped
   *      - else the size request if we haven't ever been mapped,
   *        as a substitute default size
   *      - else the current size of the window, as received from
   *        configure notifies (i.e. the current allocation)
   *
   *   If GTK_WIN_POS_CENTER_ALWAYS is active, we constrain
   *   the position request to be centered.
   */
  GtkWindowPrivate *priv = window->priv;
  GtkAllocation allocation;
  GtkWidget *widget;
  GtkContainer *container;
  GtkWindowGeometryInfo *info;
  GdkGeometry new_geometry;
  GdkWindow *gdk_window;
  guint new_flags;
  GdkRectangle new_request;
  gboolean configure_request_size_changed;
  gboolean configure_request_pos_changed;
  gboolean hints_changed; /* do we need to send these again */
  GtkWindowLastGeometryInfo saved_last_info;
  
  widget = GTK_WIDGET (window);

  gdk_window = gtk_widget_get_window (widget);
  container = GTK_CONTAINER (widget);
  info = gtk_window_get_geometry_info (window, TRUE);
  
  configure_request_size_changed = FALSE;
  configure_request_pos_changed = FALSE;
  
  gtk_window_compute_configure_request (window, &new_request,
                                        &new_geometry, &new_flags);  
  
  /* This check implies the invariant that we never set info->last
   * without setting the hints and sending off a configure request.
   *
   * If we change info->last without sending the request, we may
   * miss a request.
   */
  if (info->last.configure_request.x != new_request.x ||
      info->last.configure_request.y != new_request.y)
    configure_request_pos_changed = TRUE;

  if ((info->last.configure_request.width != new_request.width ||
       info->last.configure_request.height != new_request.height))
    configure_request_size_changed = TRUE;
  
  hints_changed = FALSE;
  
  if (!gtk_window_compare_hints (&info->last.geometry, info->last.flags,
				 &new_geometry, new_flags))
    {
      hints_changed = TRUE;
    }
  
  /* Position Constraints
   * ====================
   * 
   * POS_CENTER_ALWAYS is conceptually a constraint rather than
   * a default. The other POS_ values are used only when the
   * window is shown, not after that.
   * 
   * However, we can't implement a position constraint as
   * "anytime the window size changes, center the window"
   * because this may well end up fighting the WM or user.  In
   * fact it gets in an infinite loop with at least one WM.
   *
   * Basically, applications are in no way in a position to
   * constrain the position of a window, with one exception:
   * override redirect windows. (Really the intended purpose
   * of CENTER_ALWAYS anyhow, I would think.)
   *
   * So the way we implement this "constraint" is to say that when WE
   * cause a move or resize, i.e. we make a configure request changing
   * window size, we recompute the CENTER_ALWAYS position to reflect
   * the new window size, and include it in our request.  Also, if we
   * just turned on CENTER_ALWAYS we snap to center with a new
   * request.  Otherwise, if we are just NOTIFIED of a move or resize
   * done by someone else e.g. the window manager, we do NOT send a
   * new configure request.
   *
   * For override redirect windows, this works fine; all window
   * sizes are from our configure requests. For managed windows,
   * it is at least semi-sane, though who knows what the
   * app author is thinking.
   */

  /* This condition should be kept in sync with the condition later on
   * that determines whether we send a configure request.  i.e. we
   * should do this position constraining anytime we were going to
   * send a configure request anyhow, plus when constraints have
   * changed.
   */
  if (configure_request_pos_changed ||
      configure_request_size_changed ||
      hints_changed ||
      info->position_constraints_changed)
    {
      /* We request the constrained position if:
       *  - we were changing position, and need to clamp
       *    the change to the constraint
       *  - we're changing the size anyway
       *  - set_position() was called to toggle CENTER_ALWAYS on
       */

      gtk_window_constrain_position (window,
                                     new_request.width,
                                     new_request.height,
                                     &new_request.x,
                                     &new_request.y);
      
      /* Update whether we need to request a move */
      if (info->last.configure_request.x != new_request.x ||
          info->last.configure_request.y != new_request.y)
        configure_request_pos_changed = TRUE;
      else
        configure_request_pos_changed = FALSE;
    }

#if 0
  if (priv->type == GTK_WINDOW_TOPLEVEL)
    {
      int notify_x, notify_y;

      /* this is the position from the last configure notify */
      gdk_window_get_position (widget->window, &notify_x, &notify_y);
    
      g_message ("--- %s ---\n"
		 "last  : %d,%d\t%d x %d\n"
		 "this  : %d,%d\t%d x %d\n"
		 "alloc : %d,%d\t%d x %d\n"
		 "req   :      \t%d x %d\n"
		 "resize:      \t%d x %d\n" 
		 "size_changed: %d pos_changed: %d hints_changed: %d\n"
		 "configure_notify_received: %d\n"
		 "configure_request_count: %d\n"
		 "position_constraints_changed: %d\n",
		 priv->title ? priv->title : "(no title)",
		 info->last.configure_request.x,
		 info->last.configure_request.y,
		 info->last.configure_request.width,
		 info->last.configure_request.height,
		 new_request.x,
		 new_request.y,
		 new_request.width,
		 new_request.height,
		 notify_x, notify_y,
		 widget->allocation.width,
		 widget->allocation.height,
		 widget->requisition.width,
		 widget->requisition.height,
		 info->resize_width,
		 info->resize_height,
		 configure_request_pos_changed,
		 configure_request_size_changed,
		 hints_changed,
		 priv->configure_notify_received,
		 priv->configure_request_count,
		 info->position_constraints_changed);
    }
#endif
  
  saved_last_info = info->last;
  info->last.geometry = new_geometry;
  info->last.flags = new_flags;
  info->last.configure_request = new_request;
  
  /* need to set PPosition so the WM will look at our position,
   * but we don't want to count PPosition coming and going as a hints
   * change for future iterations. So we saved info->last prior to
   * this.
   */
  
  /* Also, if the initial position was explicitly set, then we always
   * toggle on PPosition. This makes gtk_window_move(window, 0, 0)
   * work.
   */

  /* Also, we toggle on PPosition if GTK_WIN_POS_ is in use and
   * this is an initial map
   */
  
  if ((configure_request_pos_changed ||
       info->initial_pos_set ||
       (priv->need_default_position &&
        get_effective_position (window) != GTK_WIN_POS_NONE)) &&
      (new_flags & GDK_HINT_POS) == 0)
    {
      new_flags |= GDK_HINT_POS;
      hints_changed = TRUE;
    }
  
  /* Set hints if necessary
   */
  if (hints_changed)
    gdk_window_set_geometry_hints (gdk_window,
				   &new_geometry,
				   new_flags);

  gtk_widget_get_allocation (widget, &allocation);

  /* handle resizing/moving and widget tree allocation
   */
  if (priv->configure_notify_received)
    { 
      /* If we have received a configure event since
       * the last time in this function, we need to
       * accept our new size and size_allocate child widgets.
       * (see gtk_window_configure_event() for more details).
       *
       * 1 or more configure notifies may have been received.
       * Also, configure_notify_received will only be TRUE
       * if all expected configure notifies have been received
       * (one per configure request), as an optimization.
       *
       */
      priv->configure_notify_received = FALSE;

      /* gtk_window_configure_event() filled in widget->allocation */
      gtk_widget_size_allocate (widget, &allocation);

      set_grip_position (window);
      update_grip_visibility (window);

      gdk_window_process_updates (gdk_window, TRUE);

      gdk_window_configure_finished (gdk_window);

      /* If the configure request changed, it means that
       * we either:
       *   1) coincidentally changed hints or widget properties
       *      impacting the configure request before getting
       *      a configure notify, or
       *   2) some broken widget is changing its size request
       *      during size allocation, resulting in
       *      a false appearance of changed configure request.
       *
       * For 1), we could just go ahead and ask for the
       * new size right now, but doing that for 2)
       * might well be fighting the user (and can even
       * trigger a loop). Since we really don't want to
       * do that, we requeue a resize in hopes that
       * by the time it gets handled, the child has seen
       * the light and is willing to go along with the
       * new size. (this happens for the zvt widget, since
       * the size_allocate() above will have stored the
       * requisition corresponding to the new size in the
       * zvt widget)
       *
       * This doesn't buy us anything for 1), but it shouldn't
       * hurt us too badly, since it is what would have
       * happened if we had gotten the configure event before
       * the new size had been set.
       */

      if (configure_request_size_changed ||
          configure_request_pos_changed)
        {
          /* Don't change the recorded last info after all, because we
           * haven't actually updated to the new info yet - we decided
           * to postpone our configure request until later.
           */
	  info->last = saved_last_info;
          
	  gtk_widget_queue_resize_no_redraw (widget); /* migth recurse for GTK_RESIZE_IMMEDIATE */
	}

      return;			/* Bail out, we didn't really process the move/resize */
    }
  else if ((configure_request_size_changed || hints_changed) &&
           (allocation.width != new_request.width || allocation.height != new_request.height))

    {
      /* We are in one of the following situations:
       * A. configure_request_size_changed
       *    our requisition has changed and we need a different window size,
       *    so we request it from the window manager.
       * B. !configure_request_size_changed && hints_changed
       *    the window manager rejects our size, but we have just changed the
       *    window manager hints, so there's a chance our request will
       *    be honoured this time, so we try again.
       *
       * However, if the new requisition is the same as the current allocation,
       * we don't request it again, since we won't get a ConfigureNotify back from
       * the window manager unless it decides to change our requisition. If
       * we don't get the ConfigureNotify back, the resize queue will never be run.
       */

      /* Now send the configure request */
      if (configure_request_pos_changed)
        {
          gdk_window_move_resize (gdk_window,
                                  new_request.x, new_request.y,
                                  new_request.width, new_request.height);
        }
      else  /* only size changed */
        {
          gdk_window_resize (gdk_window,
                             new_request.width, new_request.height);
        }

      if (priv->type == GTK_WINDOW_POPUP)
        {
	  GtkAllocation allocation;

	  /* Directly size allocate for override redirect (popup) windows. */
          allocation.x = 0;
	  allocation.y = 0;
	  allocation.width = new_request.width;
	  allocation.height = new_request.height;

	  gtk_widget_size_allocate (widget, &allocation);

	  gdk_window_process_updates (gdk_window, TRUE);

	  if (gtk_container_get_resize_mode (container) == GTK_RESIZE_QUEUE)
	    gtk_widget_queue_draw (widget);
	}
      else
        {
	  /* Increment the number of have-not-yet-received-notify requests */
	  priv->configure_request_count += 1;
	  gdk_window_freeze_toplevel_updates_libgtk_only (gdk_window);

	  /* for GTK_RESIZE_QUEUE toplevels, we are now awaiting a new
	   * configure event in response to our resizing request.
	   * the configure event will cause a new resize with
	   * ->configure_notify_received=TRUE.
	   * until then, we want to
	   * - discard expose events
	   * - coalesce resizes for our children
	   * - defer any window resizes until the configure event arrived
	   * to achieve this, we queue a resize for the window, but remove its
	   * resizing handler, so resizing will not be handled from the next
	   * idle handler but when the configure event arrives.
	   *
	   * FIXME: we should also dequeue the pending redraws here, since
	   * we handle those ourselves upon ->configure_notify_received==TRUE.
	   */
	  if (gtk_container_get_resize_mode (container) == GTK_RESIZE_QUEUE)
	    {
	      gtk_widget_queue_resize_no_redraw (widget);
	      _gtk_container_dequeue_resize_handler (container);
	    }
	}
    }
  else
    {
      /* Handle any position changes.
       */
      if (configure_request_pos_changed)
        {
          gdk_window_move (gdk_window,
                           new_request.x, new_request.y);
        }

      /* And run the resize queue.
       */
      gtk_container_resize_children (container);
    }
  
  /* We have now processed a move/resize since the last position
   * constraint change, setting of the initial position, or resize.
   * (Not resetting these flags here can lead to infinite loops for
   * GTK_RESIZE_IMMEDIATE containers)
   */
  info->position_constraints_changed = FALSE;
  info->initial_pos_set = FALSE;
  info->resize_width = -1;
  info->resize_height = -1;
}

/* Compare two sets of Geometry hints for equality.
 */
static gboolean
gtk_window_compare_hints (GdkGeometry *geometry_a,
			  guint        flags_a,
			  GdkGeometry *geometry_b,
			  guint        flags_b)
{
  if (flags_a != flags_b)
    return FALSE;
  
  if ((flags_a & GDK_HINT_MIN_SIZE) &&
      (geometry_a->min_width != geometry_b->min_width ||
       geometry_a->min_height != geometry_b->min_height))
    return FALSE;

  if ((flags_a & GDK_HINT_MAX_SIZE) &&
      (geometry_a->max_width != geometry_b->max_width ||
       geometry_a->max_height != geometry_b->max_height))
    return FALSE;

  if ((flags_a & GDK_HINT_BASE_SIZE) &&
      (geometry_a->base_width != geometry_b->base_width ||
       geometry_a->base_height != geometry_b->base_height))
    return FALSE;

  if ((flags_a & GDK_HINT_ASPECT) &&
      (geometry_a->min_aspect != geometry_b->min_aspect ||
       geometry_a->max_aspect != geometry_b->max_aspect))
    return FALSE;

  if ((flags_a & GDK_HINT_RESIZE_INC) &&
      (geometry_a->width_inc != geometry_b->width_inc ||
       geometry_a->height_inc != geometry_b->height_inc))
    return FALSE;

  if ((flags_a & GDK_HINT_WIN_GRAVITY) &&
      geometry_a->win_gravity != geometry_b->win_gravity)
    return FALSE;

  return TRUE;
}

void
_gtk_window_constrain_size (GtkWindow   *window,
			    gint         width,
			    gint         height,
			    gint        *new_width,
			    gint        *new_height)
{
  GtkWindowPrivate *priv;
  GtkWindowGeometryInfo *info;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  info = priv->geometry_info;
  if (info)
    {
      GdkWindowHints flags = info->last.flags;
      GdkGeometry *geometry = &info->last.geometry;
      
      gtk_window_constrain_size (window,
				 geometry,
				 flags,
				 width,
				 height,
				 new_width,
				 new_height);
    }
}

static void 
gtk_window_constrain_size (GtkWindow   *window,
			   GdkGeometry *geometry,
			   guint        flags,
			   gint         width,
			   gint         height,
			   gint        *new_width,
			   gint        *new_height)
{
  gdk_window_constrain_size (geometry, flags, width, height,
                             new_width, new_height);
}

/* Compute the set of geometry hints and flags for a window
 * based on the application set geometry, and requisition
 * of the window. gtk_widget_get_preferred_size() must have been
 * called first.
 */
static void
gtk_window_compute_hints (GtkWindow   *window,
			  GdkGeometry *new_geometry,
			  guint       *new_flags)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *widget;
  gint extra_width = 0;
  gint extra_height = 0;
  GtkWindowGeometryInfo *geometry_info;
  GtkRequisition requisition;

  widget = GTK_WIDGET (window);

  gtk_widget_get_preferred_size (widget, &requisition, NULL);
  geometry_info = gtk_window_get_geometry_info (GTK_WINDOW (widget), FALSE);

  if (geometry_info)
    {
      *new_flags = geometry_info->mask;
      *new_geometry = geometry_info->geometry;
    }
  else
    {
      *new_flags = 0;
    }
  
  if (geometry_info && geometry_info->widget)
    {
      /* If the geometry widget is set, then the hints really apply to that
       * widget. This is pretty much meaningless unless the window layout
       * is such that the rest of the window adds fixed size borders to
       * the geometry widget. Our job is to figure the size of the borders;
       * We do that by asking how big the toplevel would be if the
       * geometry widget was *really big*.
       *
       *  +----------+
       *  |AAAAAAAAA | At small sizes, the minimum sizes of widgets
       *  |GGGGG    B| in the border can confuse things
       *  |GGGGG    B|
       *  |         B|
       *  +----------+
       *
       *  +-----------+
       *  |AAAAAAAAA  | When the geometry widget is large, things are
       *  |GGGGGGGGGGB| clearer.
       *  |GGGGGGGGGGB|
       *  |GGGGGGGGGG |
       *  +-----------+
       */
#define TEMPORARY_SIZE 10000 /* 10,000 pixels should be bigger than real widget sizes */
      GtkRequisition requisition;
      int current_width, current_height;

      _gtk_widget_override_size_request (geometry_info->widget,
					 TEMPORARY_SIZE, TEMPORARY_SIZE,
					 &current_width, &current_height);
      gtk_widget_get_preferred_size (widget,
                                     &requisition, NULL);
      _gtk_widget_restore_size_request (geometry_info->widget,
					current_width, current_height);

      extra_width = requisition.width - TEMPORARY_SIZE;
      extra_height = requisition.height - TEMPORARY_SIZE;

      if (extra_width < 0 || extra_height < 0)
	{
	  g_warning("Toplevel size doesn't seem to directly depend on the "
		    "size of the geometry widget from gtk_window_set_geometry_hints(). "
		    "The geometry widget might not be in the window, or it might not "
		    "be packed into the window appropriately");
	  extra_width = MAX(extra_width, 0);
	  extra_height = MAX(extra_height, 0);
	}
#undef TEMPORARY_SIZE
    }

  /* We don't want to set GDK_HINT_POS in here, we just set it
   * in gtk_window_move_resize() when we want the position
   * honored.
   */
  
  if (*new_flags & GDK_HINT_BASE_SIZE)
    {
      new_geometry->base_width += extra_width;
      new_geometry->base_height += extra_height;
    }
  else
    {
      /* For simplicity, we always set the base hint, even when we
       * don't expect it to have any visible effect.
       * (Note: geometry_size_to_pixels() depends on this.)
       */
      *new_flags |= GDK_HINT_BASE_SIZE;

      new_geometry->base_width = extra_width;
      new_geometry->base_height = extra_height;

      /* As for X, if BASE_SIZE is not set but MIN_SIZE is set, then the
       * base size is the minimum size */
      if (*new_flags & GDK_HINT_MIN_SIZE)
	{
	  if (new_geometry->min_width > 0)
	    new_geometry->base_width += new_geometry->min_width;
	  if (new_geometry->min_height > 0)
	    new_geometry->base_height += new_geometry->min_height;
	}
    }

  /* Please use a good size for unresizable widgets, not the minimum one. */
  if (!priv->resizable)
    gtk_window_guess_default_size (window, &requisition.width, &requisition.height);

  if (*new_flags & GDK_HINT_MIN_SIZE)
    {
      if (new_geometry->min_width < 0)
	new_geometry->min_width = requisition.width;
      else
        new_geometry->min_width = MAX (requisition.width, new_geometry->min_width + extra_width);

      if (new_geometry->min_height < 0)
	new_geometry->min_height = requisition.height;
      else
	new_geometry->min_height = MAX (requisition.height, new_geometry->min_height + extra_height);
    }
  else
    {
      *new_flags |= GDK_HINT_MIN_SIZE;
      
      new_geometry->min_width = requisition.width;
      new_geometry->min_height = requisition.height;
    }
  
  if (*new_flags & GDK_HINT_MAX_SIZE)
    {
      if (new_geometry->max_width < 0)
	new_geometry->max_width = requisition.width;
      else
	new_geometry->max_width += extra_width;

      if (new_geometry->max_height < 0)
	new_geometry->max_height = requisition.height;
      else
	new_geometry->max_height += extra_height;
    }
  else if (!priv->resizable)
    {
      *new_flags |= GDK_HINT_MAX_SIZE;
      
      new_geometry->max_width = requisition.width;
      new_geometry->max_height = requisition.height;
    }

  *new_flags |= GDK_HINT_WIN_GRAVITY;
  new_geometry->win_gravity = priv->gravity;
}

/***********************
 * Redrawing functions *
 ***********************/

static gboolean
gtk_window_draw (GtkWidget *widget,
		 cairo_t   *cr)
{
  GtkWindowPrivate *priv = GTK_WINDOW (widget)->priv;
  GtkStyleContext *context;
  gboolean ret = FALSE;

  context = gtk_widget_get_style_context (widget);

  if (!gtk_widget_get_app_paintable (widget) &&
      gtk_cairo_should_draw_window (cr, gtk_widget_get_window (widget)))
    {
      gtk_render_background (context, cr, 0, 0,
			     gtk_widget_get_allocated_width (widget),
			     gtk_widget_get_allocated_height (widget));
    }

  if (GTK_WIDGET_CLASS (gtk_window_parent_class)->draw)
    ret = GTK_WIDGET_CLASS (gtk_window_parent_class)->draw (widget, cr);

  if (priv->grip_window &&
      gtk_cairo_should_draw_window (cr, priv->grip_window))
    {
      GdkRectangle rect;

      gtk_style_context_save (context);
      cairo_save (cr);

      gtk_cairo_transform_to_window (cr, widget, priv->grip_window);
      gtk_window_get_resize_grip_area (GTK_WINDOW (widget), &rect);

      gtk_style_context_remove_class (context, GTK_STYLE_CLASS_BACKGROUND);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_GRIP);
      gtk_style_context_set_junction_sides (context, get_grip_junction (widget));
      gtk_render_handle (context, cr, 0, 0, rect.width, rect.height);

      cairo_restore (cr);
      gtk_style_context_restore (context);
    }

  return ret;
}

/**
 * gtk_window_present:
 * @window: a #GtkWindow
 *
 * Presents a window to the user. This may mean raising the window
 * in the stacking order, deiconifying it, moving it to the current
 * desktop, and/or giving it the keyboard focus, possibly dependent
 * on the user's platform, window manager, and preferences.
 *
 * If @window is hidden, this function calls gtk_widget_show()
 * as well.
 * 
 * This function should be used when the user tries to open a window
 * that's already open. Say for example the preferences dialog is
 * currently open, and the user chooses Preferences from the menu
 * a second time; use gtk_window_present() to move the already-open dialog
 * where the user can see it.
 *
 * If you are calling this function in response to a user interaction,
 * it is preferable to use gtk_window_present_with_time().
 * 
 **/
void
gtk_window_present (GtkWindow *window)
{
  gtk_window_present_with_time (window, GDK_CURRENT_TIME);
}

/**
 * gtk_window_present_with_time:
 * @window: a #GtkWindow
 * @timestamp: the timestamp of the user interaction (typically a 
 *   button or key press event) which triggered this call
 *
 * Presents a window to the user in response to a user interaction.
 * If you need to present a window without a timestamp, use 
 * gtk_window_present(). See gtk_window_present() for details. 
 * 
 * Since: 2.8
 **/
void
gtk_window_present_with_time (GtkWindow *window,
			      guint32    timestamp)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *gdk_window;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  if (gtk_widget_get_visible (widget))
    {
      gdk_window = gtk_widget_get_window (widget);

      g_assert (gdk_window != NULL);

      gdk_window_show (gdk_window);

      /* Translate a timestamp of GDK_CURRENT_TIME appropriately */
      if (timestamp == GDK_CURRENT_TIME)
        {
#ifdef GDK_WINDOWING_X11
	  if (GDK_IS_X11_WINDOW(gdk_window))
	    {
	      GdkDisplay *display;

	      display = gtk_widget_get_display (GTK_WIDGET (window));
	      timestamp = gdk_x11_display_get_user_time (display);
	    }
	  else
#endif
	    timestamp = gtk_get_current_event_time ();
        }

      gdk_window_focus (gdk_window, timestamp);
    }
  else
    {
      priv->initial_timestamp = timestamp;
      gtk_widget_show (widget);
    }
}

/**
 * gtk_window_iconify:
 * @window: a #GtkWindow
 *
 * Asks to iconify (i.e. minimize) the specified @window. Note that
 * you shouldn't assume the window is definitely iconified afterward,
 * because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could deiconify it
 * again, or there may not be a window manager in which case
 * iconification isn't possible, etc. But normally the window will end
 * up iconified. Just don't write code that crashes if not.
 *
 * It's permitted to call this function before showing a window,
 * in which case the window will be iconified before it ever appears
 * onscreen.
 *
 * You can track iconification via the "window-state-event" signal
 * on #GtkWidget.
 * 
 **/
void
gtk_window_iconify (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->iconify_initially = TRUE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_iconify (toplevel);
}

/**
 * gtk_window_deiconify:
 * @window: a #GtkWindow
 *
 * Asks to deiconify (i.e. unminimize) the specified @window. Note
 * that you shouldn't assume the window is definitely deiconified
 * afterward, because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could iconify it
 * again before your code which assumes deiconification gets to run.
 *
 * You can track iconification via the "window-state-event" signal
 * on #GtkWidget.
 **/
void
gtk_window_deiconify (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->iconify_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_deiconify (toplevel);
}

/**
 * gtk_window_stick:
 * @window: a #GtkWindow
 *
 * Asks to stick @window, which means that it will appear on all user
 * desktops. Note that you shouldn't assume the window is definitely
 * stuck afterward, because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could unstick it
 * again, and some window managers do not support sticking
 * windows. But normally the window will end up stuck. Just don't
 * write code that crashes if not.
 *
 * It's permitted to call this function before showing a window.
 *
 * You can track stickiness via the "window-state-event" signal
 * on #GtkWidget.
 * 
 **/
void
gtk_window_stick (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->stick_initially = TRUE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_stick (toplevel);
}

/**
 * gtk_window_unstick:
 * @window: a #GtkWindow
 *
 * Asks to unstick @window, which means that it will appear on only
 * one of the user's desktops. Note that you shouldn't assume the
 * window is definitely unstuck afterward, because other entities
 * (e.g. the user or <link linkend="gtk-X11-arch">window
 * manager</link>) could stick it again. But normally the window will
 * end up stuck. Just don't write code that crashes if not.
 *
 * You can track stickiness via the "window-state-event" signal
 * on #GtkWidget.
 * 
 **/
void
gtk_window_unstick (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->stick_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_unstick (toplevel);
}

/**
 * gtk_window_maximize:
 * @window: a #GtkWindow
 *
 * Asks to maximize @window, so that it becomes full-screen. Note that
 * you shouldn't assume the window is definitely maximized afterward,
 * because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could unmaximize it
 * again, and not all window managers support maximization. But
 * normally the window will end up maximized. Just don't write code
 * that crashes if not.
 *
 * It's permitted to call this function before showing a window,
 * in which case the window will be maximized when it appears onscreen
 * initially.
 *
 * You can track maximization via the "window-state-event" signal
 * on #GtkWidget.
 * 
 **/
void
gtk_window_maximize (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->maximize_initially = TRUE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_maximize (toplevel);
}

/**
 * gtk_window_unmaximize:
 * @window: a #GtkWindow
 *
 * Asks to unmaximize @window. Note that you shouldn't assume the
 * window is definitely unmaximized afterward, because other entities
 * (e.g. the user or <link linkend="gtk-X11-arch">window
 * manager</link>) could maximize it again, and not all window
 * managers honor requests to unmaximize. But normally the window will
 * end up unmaximized. Just don't write code that crashes if not.
 *
 * You can track maximization via the "window-state-event" signal
 * on #GtkWidget.
 * 
 **/
void
gtk_window_unmaximize (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;
  
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->maximize_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_unmaximize (toplevel);
}

/**
 * gtk_window_fullscreen:
 * @window: a #GtkWindow
 *
 * Asks to place @window in the fullscreen state. Note that you
 * shouldn't assume the window is definitely full screen afterward,
 * because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could unfullscreen it
 * again, and not all window managers honor requests to fullscreen
 * windows. But normally the window will end up fullscreen. Just
 * don't write code that crashes if not.
 *
 * You can track the fullscreen state via the "window-state-event" signal
 * on #GtkWidget.
 * 
 * Since: 2.2
 **/
void
gtk_window_fullscreen (GtkWindow *window)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkWindow *toplevel;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->fullscreen_initially = TRUE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_fullscreen (toplevel);
}

/**
 * gtk_window_unfullscreen:
 * @window: a #GtkWindow
 *
 * Asks to toggle off the fullscreen state for @window. Note that you
 * shouldn't assume the window is definitely not full screen
 * afterward, because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could fullscreen it
 * again, and not all window managers honor requests to unfullscreen
 * windows. But normally the window will end up restored to its normal
 * state. Just don't write code that crashes if not.
 *
 * You can track the fullscreen state via the "window-state-event" signal
 * on #GtkWidget.
 * 
 * Since: 2.2
 **/
void
gtk_window_unfullscreen (GtkWindow *window)
{
  GtkWidget *widget;
  GdkWindow *toplevel;
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->fullscreen_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_unfullscreen (toplevel);
}

/**
 * gtk_window_set_keep_above:
 * @window: a #GtkWindow
 * @setting: whether to keep @window above other windows
 *
 * Asks to keep @window above, so that it stays on top. Note that
 * you shouldn't assume the window is definitely above afterward,
 * because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could not keep it above,
 * and not all window managers support keeping windows above. But
 * normally the window will end kept above. Just don't write code
 * that crashes if not.
 *
 * It's permitted to call this function before showing a window,
 * in which case the window will be kept above when it appears onscreen
 * initially.
 *
 * You can track the above state via the "window-state-event" signal
 * on #GtkWidget.
 *
 * Note that, according to the <ulink 
 * url="http://www.freedesktop.org/Standards/wm-spec">Extended Window 
 * Manager Hints</ulink> specification, the above state is mainly meant 
 * for user preferences and should not be used by applications e.g. for 
 * drawing attention to their dialogs.
 *
 * Since: 2.4
 **/
void
gtk_window_set_keep_above (GtkWindow *window,
			   gboolean   setting)
{
  GtkWidget *widget;
  GtkWindowPrivate *priv;
  GdkWindow *toplevel;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->above_initially = setting != FALSE;
  if (setting)
    priv->below_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_set_keep_above (toplevel, setting);
}

/**
 * gtk_window_set_keep_below:
 * @window: a #GtkWindow
 * @setting: whether to keep @window below other windows
 *
 * Asks to keep @window below, so that it stays in bottom. Note that
 * you shouldn't assume the window is definitely below afterward,
 * because other entities (e.g. the user or <link
 * linkend="gtk-X11-arch">window manager</link>) could not keep it below,
 * and not all window managers support putting windows below. But
 * normally the window will be kept below. Just don't write code
 * that crashes if not.
 *
 * It's permitted to call this function before showing a window,
 * in which case the window will be kept below when it appears onscreen
 * initially.
 *
 * You can track the below state via the "window-state-event" signal
 * on #GtkWidget.
 *
 * Note that, according to the <ulink 
 * url="http://www.freedesktop.org/Standards/wm-spec">Extended Window 
 * Manager Hints</ulink> specification, the above state is mainly meant 
 * for user preferences and should not be used by applications e.g. for 
 * drawing attention to their dialogs.
 *
 * Since: 2.4
 **/
void
gtk_window_set_keep_below (GtkWindow *window,
			   gboolean   setting)
{
  GtkWidget *widget;
  GtkWindowPrivate *priv;
  GdkWindow *toplevel;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;
  widget = GTK_WIDGET (window);

  priv->below_initially = setting != FALSE;
  if (setting)
    priv->above_initially = FALSE;

  toplevel = gtk_widget_get_window (widget);

  if (toplevel != NULL)
    gdk_window_set_keep_below (toplevel, setting);
}

/**
 * gtk_window_set_resizable:
 * @window: a #GtkWindow
 * @resizable: %TRUE if the user can resize this window
 *
 * Sets whether the user can resize a window. Windows are user resizable
 * by default.
 **/
void
gtk_window_set_resizable (GtkWindow *window,
                          gboolean   resizable)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  resizable = (resizable != FALSE);

  if (priv->resizable != resizable)
    {
      priv->resizable = (resizable != FALSE);

      update_grip_visibility (window);

      gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));

      g_object_notify (G_OBJECT (window), "resizable");
    }
}

/**
 * gtk_window_get_resizable:
 * @window: a #GtkWindow
 *
 * Gets the value set by gtk_window_set_resizable().
 *
 * Return value: %TRUE if the user can resize the window
 **/
gboolean
gtk_window_get_resizable (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->resizable;
}

/**
 * gtk_window_set_gravity:
 * @window: a #GtkWindow
 * @gravity: window gravity
 *
 * Window gravity defines the meaning of coordinates passed to
 * gtk_window_move(). See gtk_window_move() and #GdkGravity for
 * more details.
 *
 * The default window gravity is #GDK_GRAVITY_NORTH_WEST which will
 * typically "do what you mean."
 *
 **/
void
gtk_window_set_gravity (GtkWindow *window,
			GdkGravity gravity)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (gravity != priv->gravity)
    {
      priv->gravity = gravity;

      /* gtk_window_move_resize() will adapt gravity
       */
      gtk_widget_queue_resize_no_redraw (GTK_WIDGET (window));

      g_object_notify (G_OBJECT (window), "gravity");
    }
}

/**
 * gtk_window_get_gravity:
 * @window: a #GtkWindow
 *
 * Gets the value set by gtk_window_set_gravity().
 *
 * Return value: (transfer none): window gravity
 **/
GdkGravity
gtk_window_get_gravity (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), 0);

  return window->priv->gravity;
}

/**
 * gtk_window_begin_resize_drag:
 * @window: a #GtkWindow
 * @button: mouse button that initiated the drag
 * @edge: position of the resize control
 * @root_x: X position where the user clicked to initiate the drag, in root window coordinates
 * @root_y: Y position where the user clicked to initiate the drag
 * @timestamp: timestamp from the click event that initiated the drag
 *
 * Starts resizing a window. This function is used if an application
 * has window resizing controls. When GDK can support it, the resize
 * will be done using the standard mechanism for the <link
 * linkend="gtk-X11-arch">window manager</link> or windowing
 * system. Otherwise, GDK will try to emulate window resizing,
 * potentially not all that well, depending on the windowing system.
 * 
 **/
void
gtk_window_begin_resize_drag  (GtkWindow    *window,
                               GdkWindowEdge edge,
                               gint          button,
                               gint          root_x,
                               gint          root_y,
                               guint32       timestamp)
{
  GtkWidget *widget;
  GdkWindow *toplevel;

  g_return_if_fail (GTK_IS_WINDOW (window));
  widget = GTK_WIDGET (window);
  g_return_if_fail (gtk_widget_get_visible (widget));

  toplevel = gtk_widget_get_window (widget);

  gdk_window_begin_resize_drag (toplevel,
                                edge, button,
                                root_x, root_y,
                                timestamp);
}

/**
 * gtk_window_begin_move_drag:
 * @window: a #GtkWindow
 * @button: mouse button that initiated the drag
 * @root_x: X position where the user clicked to initiate the drag, in root window coordinates
 * @root_y: Y position where the user clicked to initiate the drag
 * @timestamp: timestamp from the click event that initiated the drag
 *
 * Starts moving a window. This function is used if an application has
 * window movement grips. When GDK can support it, the window movement
 * will be done using the standard mechanism for the <link
 * linkend="gtk-X11-arch">window manager</link> or windowing
 * system. Otherwise, GDK will try to emulate window movement,
 * potentially not all that well, depending on the windowing system.
 * 
 **/
void
gtk_window_begin_move_drag  (GtkWindow *window,
                             gint       button,
                             gint       root_x,
                             gint       root_y,
                             guint32    timestamp)
{
  GtkWidget *widget;
  GdkWindow *toplevel;

  g_return_if_fail (GTK_IS_WINDOW (window));
  widget = GTK_WIDGET (window);
  g_return_if_fail (gtk_widget_get_visible (widget));

  toplevel = gtk_widget_get_window (widget);

  gdk_window_begin_move_drag (toplevel,
                              button,
                              root_x, root_y,
                              timestamp);
}

/**
 * gtk_window_set_screen:
 * @window: a #GtkWindow.
 * @screen: a #GdkScreen.
 *
 * Sets the #GdkScreen where the @window is displayed; if
 * the window is already mapped, it will be unmapped, and
 * then remapped on the new screen.
 *
 * Since: 2.2
 */
void
gtk_window_set_screen (GtkWindow *window,
		       GdkScreen *screen)
{
  GtkWindowPrivate *priv;
  GtkWidget *widget;
  GdkScreen *previous_screen;
  gboolean was_mapped;

  g_return_if_fail (GTK_IS_WINDOW (window));
  g_return_if_fail (GDK_IS_SCREEN (screen));

  priv = window->priv;

  if (screen == priv->screen)
    return;

  widget = GTK_WIDGET (window);

  previous_screen = priv->screen;
  was_mapped = gtk_widget_get_mapped (widget);

  if (was_mapped)
    gtk_widget_unmap (widget);
  if (gtk_widget_get_realized (widget))
    gtk_widget_unrealize (widget);

  gtk_window_free_key_hash (window);
  priv->screen = screen;
  gtk_widget_reset_rc_styles (widget);
  if (screen != previous_screen)
    {
      if (previous_screen)
        {
          g_signal_handlers_disconnect_by_func (previous_screen,
                                                gtk_window_on_composited_changed, window);
#ifdef GDK_WINDOWING_X11
          g_signal_handlers_disconnect_by_func (gtk_settings_get_for_screen (previous_screen),
                                                gtk_window_on_theme_variant_changed, window);
#endif
        }
      g_signal_connect (screen, "composited-changed",
                        G_CALLBACK (gtk_window_on_composited_changed), window);
#ifdef GDK_WINDOWING_X11
      g_signal_connect (gtk_settings_get_for_screen (screen),
                        "notify::gtk-application-prefer-dark-theme",
                        G_CALLBACK (gtk_window_on_theme_variant_changed), window);
#endif

      _gtk_widget_propagate_screen_changed (widget, previous_screen);
      _gtk_widget_propagate_composited_changed (widget);
    }
  g_object_notify (G_OBJECT (window), "screen");

  if (was_mapped)
    gtk_widget_map (widget);
}

static void
gtk_window_set_theme_variant (GtkWindow *window)
{
#ifdef GDK_WINDOWING_X11
  GdkWindow *gdk_window;
  gboolean   dark_theme_requested;

  g_object_get (gtk_settings_get_for_screen (window->priv->screen),
                "gtk-application-prefer-dark-theme", &dark_theme_requested,
                NULL);

  gdk_window = gtk_widget_get_window (GTK_WIDGET (window));

  if (GDK_IS_X11_WINDOW (gdk_window))
    gdk_x11_window_set_theme_variant (gdk_window,
                                      dark_theme_requested ? "dark" : NULL);
#endif
}

static void
gtk_window_on_theme_variant_changed (GtkSettings *settings,
                                     GParamSpec  *pspec,
                                     GtkWindow   *window)
{
  if (window->priv->type == GTK_WINDOW_TOPLEVEL)
    gtk_window_set_theme_variant (window);
}

static void
gtk_window_on_composited_changed (GdkScreen *screen,
				  GtkWindow *window)
{
  gtk_widget_queue_draw (GTK_WIDGET (window));
  
  _gtk_widget_propagate_composited_changed (GTK_WIDGET (window));
}

static GdkScreen *
gtk_window_check_screen (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;

  if (priv->screen)
    return priv->screen;
  else
    {
      g_warning ("Screen for GtkWindow not set; you must always set\n"
		 "a screen for a GtkWindow before using the window");
      return NULL;
    }
}

/**
 * gtk_window_get_screen:
 * @window: a #GtkWindow.
 *
 * Returns the #GdkScreen associated with @window.
 *
 * Return value: (transfer none): a #GdkScreen.
 *
 * Since: 2.2
 */
GdkScreen*
gtk_window_get_screen (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);

  return window->priv->screen;
}

/**
 * gtk_window_is_active:
 * @window: a #GtkWindow
 * 
 * Returns whether the window is part of the current active toplevel.
 * (That is, the toplevel window receiving keystrokes.)
 * The return value is %TRUE if the window is active toplevel
 * itself, but also if it is, say, a #GtkPlug embedded in the active toplevel.
 * You might use this function if you wanted to draw a widget
 * differently in an active window from a widget in an inactive window.
 * See gtk_window_has_toplevel_focus()
 * 
 * Return value: %TRUE if the window part of the current active window.
 *
 * Since: 2.4
 **/
gboolean
gtk_window_is_active (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->is_active;
}

/**
 * gtk_window_has_toplevel_focus:
 * @window: a #GtkWindow
 * 
 * Returns whether the input focus is within this GtkWindow.
 * For real toplevel windows, this is identical to gtk_window_is_active(),
 * but for embedded windows, like #GtkPlug, the results will differ.
 * 
 * Return value: %TRUE if the input focus is within this GtkWindow
 *
 * Since: 2.4
 **/
gboolean
gtk_window_has_toplevel_focus (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->has_toplevel_focus;
}


/**
 * SECTION:gtkwindowgroup
 * @Short_description: Limit the effect of grabs
 * @Title: GtkWindowGroup
 *
 * #GtkWindowGroup objects are referenced by each window in the group,
 * so once you have added all windows to a #GtkWindowGroup, you can drop
 * the initial reference to the window group with g_object_unref(). If the
 * windows in the window group are subsequently destroyed, then they will
 * be removed from the window group and drop their references on the window
 * group; when all window have been removed, the window group will be
 * freed.
 */

G_DEFINE_TYPE (GtkWindowGroup, gtk_window_group, G_TYPE_OBJECT)

static void
gtk_window_group_init (GtkWindowGroup *group)
{
  group->priv = G_TYPE_INSTANCE_GET_PRIVATE (group,
                                             GTK_TYPE_WINDOW_GROUP,
                                             GtkWindowGroupPrivate);
}

static void
gtk_window_group_class_init (GtkWindowGroupClass *klass)
{
  g_type_class_add_private (klass, sizeof (GtkWindowGroupPrivate));
}

/**
 * gtk_window_group_new:
 * 
 * Creates a new #GtkWindowGroup object. Grabs added with
 * gtk_grab_add() only affect windows within the same #GtkWindowGroup.
 * 
 * Return value: a new #GtkWindowGroup. 
 **/
GtkWindowGroup *
gtk_window_group_new (void)
{
  return g_object_new (GTK_TYPE_WINDOW_GROUP, NULL);
}

static void
window_group_cleanup_grabs (GtkWindowGroup *group,
                            GtkWindow      *window)
{
  GtkWindowGroupPrivate *priv;
  GtkDeviceGrabInfo *info;
  GSList *tmp_list;
  GSList *to_remove = NULL;

  priv = group->priv;

  tmp_list = priv->grabs;
  while (tmp_list)
    {
      if (gtk_widget_get_toplevel (tmp_list->data) == (GtkWidget*) window)
	to_remove = g_slist_prepend (to_remove, g_object_ref (tmp_list->data));
      tmp_list = tmp_list->next;
    }

  while (to_remove)
    {
      gtk_grab_remove (to_remove->data);
      g_object_unref (to_remove->data);
      to_remove = g_slist_delete_link (to_remove, to_remove);
    }

  tmp_list = priv->device_grabs;

  while (tmp_list)
    {
      info = tmp_list->data;

      if (gtk_widget_get_toplevel (info->widget) == (GtkWidget *) window)
        to_remove = g_slist_prepend (to_remove, info);

      tmp_list = tmp_list->next;
    }

  while (to_remove)
    {
      info = to_remove->data;

      gtk_device_grab_remove (info->widget, info->device);
      to_remove = g_slist_delete_link (to_remove, to_remove);
    }
}

/**
 * gtk_window_group_add_window:
 * @window_group: a #GtkWindowGroup
 * @window: the #GtkWindow to add
 * 
 * Adds a window to a #GtkWindowGroup. 
 **/
void
gtk_window_group_add_window (GtkWindowGroup *window_group,
			     GtkWindow      *window)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW_GROUP (window_group));
  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  if (priv->group != window_group)
    {
      g_object_ref (window);
      g_object_ref (window_group);

      if (priv->group)
	gtk_window_group_remove_window (priv->group, window);
      else
	window_group_cleanup_grabs (gtk_window_get_group (NULL), window);

      priv->group = window_group;

      g_object_unref (window);
    }
}

/**
 * gtk_window_group_remove_window:
 * @window_group: a #GtkWindowGroup
 * @window: the #GtkWindow to remove
 * 
 * Removes a window from a #GtkWindowGroup.
 **/
void
gtk_window_group_remove_window (GtkWindowGroup *window_group,
				GtkWindow      *window)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW_GROUP (window_group));
  g_return_if_fail (GTK_IS_WINDOW (window));
  priv = window->priv;
  g_return_if_fail (priv->group == window_group);

  g_object_ref (window);

  window_group_cleanup_grabs (window_group, window);
  priv->group = NULL;

  g_object_unref (window_group);
  g_object_unref (window);
}

/**
 * gtk_window_group_list_windows:
 * @window_group: a #GtkWindowGroup
 *
 * Returns a list of the #GtkWindows that belong to @window_group.
 *
 * Returns: (element-type GtkWindow) (transfer container): A
 *   newly-allocated list of windows inside the group.
 *
 * Since: 2.14
 **/
GList *
gtk_window_group_list_windows (GtkWindowGroup *window_group)
{
  GList *toplevels, *toplevel, *group_windows;

  g_return_val_if_fail (GTK_IS_WINDOW_GROUP (window_group), NULL);

  group_windows = NULL;
  toplevels = gtk_window_list_toplevels ();

  for (toplevel = toplevels; toplevel; toplevel = toplevel->next)
    {
      GtkWindow *window = toplevel->data;

      if (window_group == window->priv->group)
	group_windows = g_list_prepend (group_windows, window);
    }

  g_list_free (toplevels);

  return g_list_reverse (group_windows);
}

/**
 * gtk_window_get_group:
 * @window: (allow-none): a #GtkWindow, or %NULL
 *
 * Returns the group for @window or the default group, if
 * @window is %NULL or if @window does not have an explicit
 * window group.
 *
 * Returns: (transfer none): the #GtkWindowGroup for a window or the default group
 *
 * Since: 2.10
 */
GtkWindowGroup *
gtk_window_get_group (GtkWindow *window)
{
  if (window && window->priv->group)
    return window->priv->group;
  else
    {
      static GtkWindowGroup *default_group = NULL;

      if (!default_group)
	default_group = gtk_window_group_new ();

      return default_group;
    }
}

/**
 * gtk_window_has_group:
 * @window: a #GtkWindow
 *
 * Returns whether @window has an explicit window group.
 *
 * Return value: %TRUE if @window has an explicit window group.
 *
 * Since 2.22
 **/
gboolean
gtk_window_has_group (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->group != NULL;
}

/**
 * gtk_window_group_get_current_grab:
 * @window_group: a #GtkWindowGroup
 *
 * Gets the current grab widget of the given group,
 * see gtk_grab_add().
 *
 * Returns: (transfer none): the current grab widget of the group
 *
 * Since: 2.22
 */
GtkWidget *
gtk_window_group_get_current_grab (GtkWindowGroup *window_group)
{
  g_return_val_if_fail (GTK_IS_WINDOW_GROUP (window_group), NULL);

  if (window_group->priv->grabs)
    return GTK_WIDGET (window_group->priv->grabs->data);
  return NULL;
}

void
_gtk_window_group_add_grab (GtkWindowGroup *window_group,
                            GtkWidget      *widget)
{
  GtkWindowGroupPrivate *priv;

  priv = window_group->priv;
  priv->grabs = g_slist_prepend (priv->grabs, widget);
}

void
_gtk_window_group_remove_grab (GtkWindowGroup *window_group,
                               GtkWidget      *widget)
{
  GtkWindowGroupPrivate *priv;

  priv = window_group->priv;
  priv->grabs = g_slist_remove (priv->grabs, widget);
}


void
_gtk_window_group_add_device_grab (GtkWindowGroup *window_group,
                                   GtkWidget      *widget,
                                   GdkDevice      *device,
                                   gboolean        block_others)
{
  GtkWindowGroupPrivate *priv;
  GtkDeviceGrabInfo *info;

  priv = window_group->priv;

  info = g_slice_new0 (GtkDeviceGrabInfo);
  info->widget = widget;
  info->device = device;
  info->block_others = block_others;

  priv->device_grabs = g_slist_prepend (priv->device_grabs, info);
}

void
_gtk_window_group_remove_device_grab (GtkWindowGroup *window_group,
                                      GtkWidget      *widget,
                                      GdkDevice      *device)
{
  GtkWindowGroupPrivate *priv;
  GtkDeviceGrabInfo *info;
  GSList *list, *node = NULL;
  GdkDevice *other_device;

  priv = window_group->priv;
  other_device = gdk_device_get_associated_device (device);
  list = priv->device_grabs;

  while (list)
    {
      info = list->data;

      if (info->widget == widget &&
          (info->device == device ||
           info->device == other_device))
        {
          node = list;
          break;
        }

      list = list->next;
    }

  if (node)
    {
      info = node->data;

      priv->device_grabs = g_slist_delete_link (priv->device_grabs, node);
      g_slice_free (GtkDeviceGrabInfo, info);
    }
}

/**
 * gtk_window_group_get_current_device_grab:
 * @window_group: a #GtkWindowGroup
 * @device: a #GdkDevice
 *
 * Returns the current grab widget for @device, or %NULL if none.
 *
 * Returns: (transfer none): The grab widget, or %NULL
 *
 * Since: 3.0
 */
GtkWidget *
gtk_window_group_get_current_device_grab (GtkWindowGroup *window_group,
                                          GdkDevice      *device)
{
  GtkWindowGroupPrivate *priv;
  GtkDeviceGrabInfo *info;
  GdkDevice *other_device;
  GSList *list;

  g_return_val_if_fail (GTK_IS_WINDOW_GROUP (window_group), NULL);
  g_return_val_if_fail (GDK_IS_DEVICE (device), NULL);

  priv = window_group->priv;
  list = priv->device_grabs;
  other_device = gdk_device_get_associated_device (device);

  while (list)
    {
      info = list->data;
      list = list->next;

      if (info->device == device ||
          info->device == other_device)
        return info->widget;
    }

  return NULL;
}

gboolean
_gtk_window_group_widget_is_blocked_for_device (GtkWindowGroup *window_group,
                                                GtkWidget      *widget,
                                                GdkDevice      *device)
{
  GtkWindowGroupPrivate *priv;
  GtkDeviceGrabInfo *info;
  GdkDevice *other_device;
  GSList *list;

  priv = window_group->priv;
  other_device = gdk_device_get_associated_device (device);
  list = priv->device_grabs;

  while (list)
    {
      info = list->data;
      list = list->next;

      /* Look for blocking grabs on other device pairs
       * that have the passed widget within the GTK+ grab.
       */
      if (info->block_others &&
          info->device != device &&
          info->device != other_device &&
          (info->widget == widget ||
           gtk_widget_is_ancestor (widget, info->widget)))
        return TRUE;
    }

  return FALSE;
}

/*
  Derived from XParseGeometry() in XFree86  

  Copyright 1985, 1986, 1987,1998  The Open Group

  All Rights Reserved.

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  Except as contained in this notice, the name of The Open Group shall
  not be used in advertising or otherwise to promote the sale, use or
  other dealings in this Software without prior written authorization
  from The Open Group.
*/


/*
 *    XParseGeometry parses strings of the form
 *   "=<width>x<height>{+-}<xoffset>{+-}<yoffset>", where
 *   width, height, xoffset, and yoffset are unsigned integers.
 *   Example:  "=80x24+300-49"
 *   The equal sign is optional.
 *   It returns a bitmask that indicates which of the four values
 *   were actually found in the string.  For each value found,
 *   the corresponding argument is updated;  for each value
 *   not found, the corresponding argument is left unchanged. 
 */

/* The following code is from Xlib, and is minimally modified, so we
 * can track any upstream changes if required.  Don't change this
 * code. Or if you do, put in a huge comment marking which thing
 * changed.
 */

static int
read_int (gchar   *string,
          gchar  **next)
{
  int result = 0;
  int sign = 1;
  
  if (*string == '+')
    string++;
  else if (*string == '-')
    {
      string++;
      sign = -1;
    }

  for (; (*string >= '0') && (*string <= '9'); string++)
    {
      result = (result * 10) + (*string - '0');
    }

  *next = string;

  if (sign >= 0)
    return (result);
  else
    return (-result);
}

/* 
 * Bitmask returned by XParseGeometry().  Each bit tells if the corresponding
 * value (x, y, width, height) was found in the parsed string.
 */
#define NoValue         0x0000
#define XValue          0x0001
#define YValue          0x0002
#define WidthValue      0x0004
#define HeightValue     0x0008
#define AllValues       0x000F
#define XNegative       0x0010
#define YNegative       0x0020

/* Try not to reformat/modify, so we can compare/sync with X sources */
static int
gtk_XParseGeometry (const char   *string,
                    int          *x,
                    int          *y,
                    unsigned int *width,   
                    unsigned int *height)  
{
  int mask = NoValue;
  char *strind;
  unsigned int tempWidth, tempHeight;
  int tempX, tempY;
  char *nextCharacter;

  /* These initializations are just to silence gcc */
  tempWidth = 0;
  tempHeight = 0;
  tempX = 0;
  tempY = 0;
  
  if ( (string == NULL) || (*string == '\0')) return(mask);
  if (*string == '=')
    string++;  /* ignore possible '=' at beg of geometry spec */

  strind = (char *)string;
  if (*strind != '+' && *strind != '-' && *strind != 'x') {
    tempWidth = read_int(strind, &nextCharacter);
    if (strind == nextCharacter) 
      return (0);
    strind = nextCharacter;
    mask |= WidthValue;
  }

  if (*strind == 'x' || *strind == 'X') {	
    strind++;
    tempHeight = read_int(strind, &nextCharacter);
    if (strind == nextCharacter)
      return (0);
    strind = nextCharacter;
    mask |= HeightValue;
  }

  if ((*strind == '+') || (*strind == '-')) {
    if (*strind == '-') {
      strind++;
      tempX = -read_int(strind, &nextCharacter);
      if (strind == nextCharacter)
        return (0);
      strind = nextCharacter;
      mask |= XNegative;

    }
    else
      {	strind++;
      tempX = read_int(strind, &nextCharacter);
      if (strind == nextCharacter)
        return(0);
      strind = nextCharacter;
      }
    mask |= XValue;
    if ((*strind == '+') || (*strind == '-')) {
      if (*strind == '-') {
        strind++;
        tempY = -read_int(strind, &nextCharacter);
        if (strind == nextCharacter)
          return(0);
        strind = nextCharacter;
        mask |= YNegative;

      }
      else
        {
          strind++;
          tempY = read_int(strind, &nextCharacter);
          if (strind == nextCharacter)
            return(0);
          strind = nextCharacter;
        }
      mask |= YValue;
    }
  }
	
  /* If strind isn't at the end of the string the it's an invalid
		geometry specification. */

  if (*strind != '\0') return (0);

  if (mask & XValue)
    *x = tempX;
  if (mask & YValue)
    *y = tempY;
  if (mask & WidthValue)
    *width = tempWidth;
  if (mask & HeightValue)
    *height = tempHeight;
  return (mask);
}

/**
 * gtk_window_parse_geometry:
 * @window: a #GtkWindow
 * @geometry: geometry string
 * 
 * Parses a standard X Window System geometry string - see the
 * manual page for X (type 'man X') for details on this.
 * gtk_window_parse_geometry() does work on all GTK+ ports
 * including Win32 but is primarily intended for an X environment.
 *
 * If either a size or a position can be extracted from the
 * geometry string, gtk_window_parse_geometry() returns %TRUE
 * and calls gtk_window_set_default_size() and/or gtk_window_move()
 * to resize/move the window.
 *
 * If gtk_window_parse_geometry() returns %TRUE, it will also
 * set the #GDK_HINT_USER_POS and/or #GDK_HINT_USER_SIZE hints
 * indicating to the window manager that the size/position of
 * the window was user-specified. This causes most window
 * managers to honor the geometry.
 *
 * Note that for gtk_window_parse_geometry() to work as expected, it has
 * to be called when the window has its "final" size, i.e. after calling
 * gtk_widget_show_all() on the contents and gtk_window_set_geometry_hints()
 * on the window.
 * |[
 * #include <gtk/gtk.h>
 *    
 * static void
 * fill_with_content (GtkWidget *vbox)
 * {
 *   /&ast; fill with content... &ast;/
 * }
 *    
 * int
 * main (int argc, char *argv[])
 * {
 *   GtkWidget *window, *vbox;
 *   GdkGeometry size_hints = {
 *     100, 50, 0, 0, 100, 50, 10, 10, 0.0, 0.0, GDK_GRAVITY_NORTH_WEST  
 *   };
 *    
 *   gtk_init (&argc, &argv);
 *   
 *   window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
 *   vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE, 0);
 *   
 *   gtk_container_add (GTK_CONTAINER (window), vbox);
 *   fill_with_content (vbox);
 *   gtk_widget_show_all (vbox);
 *   
 *   gtk_window_set_geometry_hints (GTK_WINDOW (window),
 * 	  			    window,
 * 				    &size_hints,
 * 				    GDK_HINT_MIN_SIZE | 
 * 				    GDK_HINT_BASE_SIZE | 
 * 				    GDK_HINT_RESIZE_INC);
 *   
 *   if (argc &gt; 1)
 *     {
 *       if (!gtk_window_parse_geometry (GTK_WINDOW (window), argv[1]))
 *         fprintf (stderr, "Failed to parse '%s'\n", argv[1]);
 *     }
 *    
 *   gtk_widget_show_all (window);
 *   gtk_main ();
 *    
 *   return 0;
 * }
 * ]|
 *
 * Return value: %TRUE if string was parsed successfully
 **/
gboolean
gtk_window_parse_geometry (GtkWindow   *window,
                           const gchar *geometry)
{
  gint result, x = 0, y = 0;
  guint w, h;
  GtkWidget *child;
  GdkGravity grav;
  gboolean size_set, pos_set;
  GdkScreen *screen;
  
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);
  g_return_val_if_fail (geometry != NULL, FALSE);

  child = gtk_bin_get_child (GTK_BIN (window));
  if (!child || !gtk_widget_get_visible (child))
    g_warning ("gtk_window_parse_geometry() called on a window with no "
	       "visible children; the window should be set up before "
	       "gtk_window_parse_geometry() is called.");

  screen = gtk_window_check_screen (window);
  
  result = gtk_XParseGeometry (geometry, &x, &y, &w, &h);

  size_set = FALSE;
  if ((result & WidthValue) || (result & HeightValue))
    {
      gtk_window_set_default_size_internal (window, 
					    TRUE, result & WidthValue ? w : -1,
					    TRUE, result & HeightValue ? h : -1, 
					    TRUE);
      size_set = TRUE;
    }

  gtk_window_get_size (window, (gint *)&w, (gint *)&h);
  
  grav = GDK_GRAVITY_NORTH_WEST;

  if ((result & XNegative) && (result & YNegative))
    grav = GDK_GRAVITY_SOUTH_EAST;
  else if (result & XNegative)
    grav = GDK_GRAVITY_NORTH_EAST;
  else if (result & YNegative)
    grav = GDK_GRAVITY_SOUTH_WEST;

  if ((result & XValue) == 0)
    x = 0;

  if ((result & YValue) == 0)
    y = 0;

  if (grav == GDK_GRAVITY_SOUTH_WEST ||
      grav == GDK_GRAVITY_SOUTH_EAST)
    y = gdk_screen_get_height (screen) - h + y;

  if (grav == GDK_GRAVITY_SOUTH_EAST ||
      grav == GDK_GRAVITY_NORTH_EAST)
    x = gdk_screen_get_width (screen) - w + x;

  /* we don't let you put a window offscreen; maybe some people would
   * prefer to be able to, but it's kind of a bogus thing to do.
   */
  if (y < 0)
    y = 0;

  if (x < 0)
    x = 0;

  pos_set = FALSE;
  if ((result & XValue) || (result & YValue))
    {
      gtk_window_set_gravity (window, grav);
      gtk_window_move (window, x, y);
      pos_set = TRUE;
    }

  if (size_set || pos_set)
    {
      /* Set USSize, USPosition hints */
      GtkWindowGeometryInfo *info;

      info = gtk_window_get_geometry_info (window, TRUE);

      if (pos_set)
        info->mask |= GDK_HINT_USER_POS;
      if (size_set)
        info->mask |= GDK_HINT_USER_SIZE;
    }
  
  return result != 0;
}

static void
gtk_window_mnemonic_hash_foreach (guint      keyval,
				  GSList    *targets,
				  gpointer   data)
{
  struct {
    GtkWindow *window;
    GtkWindowKeysForeachFunc func;
    gpointer func_data;
  } *info = data;

  (*info->func) (info->window, keyval, info->window->priv->mnemonic_modifier, TRUE, info->func_data);
}

void
_gtk_window_keys_foreach (GtkWindow                *window,
			  GtkWindowKeysForeachFunc func,
			  gpointer                 func_data)
{
  GSList *groups;
  GtkMnemonicHash *mnemonic_hash;

  struct {
    GtkWindow *window;
    GtkWindowKeysForeachFunc func;
    gpointer func_data;
  } info;

  info.window = window;
  info.func = func;
  info.func_data = func_data;

  mnemonic_hash = gtk_window_get_mnemonic_hash (window, FALSE);
  if (mnemonic_hash)
    _gtk_mnemonic_hash_foreach (mnemonic_hash,
				gtk_window_mnemonic_hash_foreach, &info);

  groups = gtk_accel_groups_from_object (G_OBJECT (window));
  while (groups)
    {
      GtkAccelGroup *group = groups->data;
      gint i;

      for (i = 0; i < group->priv->n_accels; i++)
	{
	  GtkAccelKey *key = &group->priv->priv_accels[i].key;
	  
	  if (key->accel_key)
	    (*func) (window, key->accel_key, key->accel_mods, FALSE, func_data);
	}
      
      groups = groups->next;
    }
}

static void
gtk_window_keys_changed (GtkWindow *window)
{
  gtk_window_free_key_hash (window);
  gtk_window_get_key_hash (window);
}

typedef struct _GtkWindowKeyEntry GtkWindowKeyEntry;

struct _GtkWindowKeyEntry
{
  guint keyval;
  guint modifiers;
  guint is_mnemonic : 1;
};

static void 
window_key_entry_destroy (gpointer data)
{
  g_slice_free (GtkWindowKeyEntry, data);
}

static void
add_to_key_hash (GtkWindow      *window,
		 guint           keyval,
		 GdkModifierType modifiers,
		 gboolean        is_mnemonic,
		 gpointer        data)
{
  GtkKeyHash *key_hash = data;

  GtkWindowKeyEntry *entry = g_slice_new (GtkWindowKeyEntry);

  entry->keyval = keyval;
  entry->modifiers = modifiers;
  entry->is_mnemonic = is_mnemonic;

  /* GtkAccelGroup stores lowercased accelerators. To deal
   * with this, if <Shift> was specified, uppercase.
   */
  if (modifiers & GDK_SHIFT_MASK)
    {
      if (keyval == GDK_KEY_Tab)
	keyval = GDK_KEY_ISO_Left_Tab;
      else
	keyval = gdk_keyval_to_upper (keyval);
    }
  
  _gtk_key_hash_add_entry (key_hash, keyval, entry->modifiers, entry);
}

static GtkKeyHash *
gtk_window_get_key_hash (GtkWindow *window)
{
  GdkScreen *screen = gtk_window_check_screen (window);
  GtkKeyHash *key_hash = g_object_get_qdata (G_OBJECT (window), quark_gtk_window_key_hash);
  
  if (key_hash)
    return key_hash;
  
  key_hash = _gtk_key_hash_new (gdk_keymap_get_for_display (gdk_screen_get_display (screen)),
				(GDestroyNotify)window_key_entry_destroy);
  _gtk_window_keys_foreach (window, add_to_key_hash, key_hash);
  g_object_set_qdata (G_OBJECT (window), quark_gtk_window_key_hash, key_hash);

  return key_hash;
}

static void
gtk_window_free_key_hash (GtkWindow *window)
{
  GtkKeyHash *key_hash = g_object_get_qdata (G_OBJECT (window), quark_gtk_window_key_hash);
  if (key_hash)
    {
      _gtk_key_hash_free (key_hash);
      g_object_set_qdata (G_OBJECT (window), quark_gtk_window_key_hash, NULL);
    }
}

/**
 * gtk_window_activate_key:
 * @window:  a #GtkWindow
 * @event:   a #GdkEventKey
 *
 * Activates mnemonics and accelerators for this #GtkWindow. This is normally
 * called by the default ::key_press_event handler for toplevel windows,
 * however in some cases it may be useful to call this directly when
 * overriding the standard key handling for a toplevel window.
 *
 * Return value: %TRUE if a mnemonic or accelerator was found and activated.
 *
 * Since: 2.4
 */
gboolean
gtk_window_activate_key (GtkWindow   *window,
			 GdkEventKey *event)
{
  GtkKeyHash *key_hash;
  GtkWindowKeyEntry *found_entry = NULL;
  gboolean enable_mnemonics;
  gboolean enable_accels;

  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  key_hash = gtk_window_get_key_hash (window);

  if (key_hash)
    {
      GSList *tmp_list;
      GSList *entries = _gtk_key_hash_lookup (key_hash,
					      event->hardware_keycode,
					      event->state,
					      gtk_accelerator_get_default_mod_mask (),
					      event->group);

      g_object_get (gtk_widget_get_settings (GTK_WIDGET (window)),
                    "gtk-enable-mnemonics", &enable_mnemonics,
                    "gtk-enable-accels", &enable_accels,
                    NULL);

      for (tmp_list = entries; tmp_list; tmp_list = tmp_list->next)
	{
	  GtkWindowKeyEntry *entry = tmp_list->data;
	  if (entry->is_mnemonic)
            {
              if (enable_mnemonics)
	        {
	          found_entry = entry;
	          break;
	        }
            }
          else 
            {
              if (enable_accels && !found_entry)
                {
	          found_entry = entry;
                }
            }
	}

      g_slist_free (entries);
    }

  if (found_entry)
    {
      if (found_entry->is_mnemonic)
        {
          if (enable_mnemonics)
            return gtk_window_mnemonic_activate (window, found_entry->keyval,
                                                 found_entry->modifiers);
        }
      else
        {
          if (enable_accels)
            return gtk_accel_groups_activate (G_OBJECT (window), found_entry->keyval,
                                              found_entry->modifiers);
        }
    }

  return FALSE;
}

static void
window_update_has_focus (GtkWindow *window)
{
  GtkWindowPrivate *priv = window->priv;
  GtkWidget *widget = GTK_WIDGET (window);
  gboolean has_focus = priv->has_toplevel_focus && priv->is_active;

  if (has_focus != priv->has_focus)
    {
      priv->has_focus = has_focus;

      if (has_focus)
	{
	  if (priv->focus_widget &&
	      priv->focus_widget != widget &&
	      !gtk_widget_has_focus (priv->focus_widget))
	    do_focus_change (priv->focus_widget, TRUE);
	}
      else
	{
	  if (priv->focus_widget &&
	      priv->focus_widget != widget &&
	      gtk_widget_has_focus (priv->focus_widget))
	    do_focus_change (priv->focus_widget, FALSE);
	}
    }
}

/**
 * _gtk_window_set_is_active:
 * @window: a #GtkWindow
 * @is_active: %TRUE if the window is in the currently active toplevel
 * 
 * Internal function that sets whether the #GtkWindow is part
 * of the currently active toplevel window (taking into account inter-process
 * embedding.)
 **/
void
_gtk_window_set_is_active (GtkWindow *window,
			   gboolean   is_active)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  is_active = is_active != FALSE;

  if (is_active != priv->is_active)
    {
      priv->is_active = is_active;
      window_update_has_focus (window);

      g_object_notify (G_OBJECT (window), "is-active");
    }
}

/**
 * _gtk_window_set_is_toplevel:
 * @window: a #GtkWindow
 * @is_toplevel: %TRUE if the window is still a real toplevel (nominally a
 * child of the root window); %FALSE if it is not (for example, for an
 * in-process, parented GtkPlug)
 *
 * Internal function used by #GtkPlug when it gets parented/unparented by a
 * #GtkSocket.  This keeps the @window's #GTK_WINDOW_TOPLEVEL flag in sync
 * with the global list of toplevel windows.
 */
void
_gtk_window_set_is_toplevel (GtkWindow *window,
                             gboolean   is_toplevel)
{
  GtkWidget *widget;
  GtkWidget *toplevel;

  widget = GTK_WIDGET (window);

  if (gtk_widget_is_toplevel (widget))
    g_assert (g_slist_find (toplevel_list, window) != NULL);
  else
    g_assert (g_slist_find (toplevel_list, window) == NULL);

  if (is_toplevel == gtk_widget_is_toplevel (widget))
    return;

  if (is_toplevel)
    {
      /* Pass through regular pathways of an embedded toplevel
       * to go through unmapping and hiding the widget before
       * becomming a toplevel again.
       *
       * We remain hidden after becomming toplevel in order to
       * avoid problems during an embedded toplevel's dispose cycle
       * (When a toplevel window is shown it tries to grab focus again,
       * this causes problems while disposing).
       */
      gtk_widget_hide (widget);

      /* Save the toplevel this widget was previously anchored into before
       * propagating a hierarchy-changed.
       *
       * Usually this happens by way of gtk_widget_unparent() and we are
       * already unanchored at this point, just adding this clause incase
       * things happen differently.
       */
      toplevel = gtk_widget_get_toplevel (widget);
      if (!gtk_widget_is_toplevel (toplevel))
        toplevel = NULL;

      _gtk_widget_set_is_toplevel (widget, TRUE);

      /* When a window becomes toplevel after being embedded and anchored
       * into another window we need to unset its anchored flag so that
       * the hierarchy changed signal kicks in properly.
       */
      _gtk_widget_set_anchored (widget, FALSE);
      _gtk_widget_propagate_hierarchy_changed (widget, toplevel);

      toplevel_list = g_slist_prepend (toplevel_list, window);
    }
  else
    {
      _gtk_widget_set_is_toplevel (widget, FALSE);
      toplevel_list = g_slist_remove (toplevel_list, window);

      _gtk_widget_propagate_hierarchy_changed (widget, widget);
    }
}

/**
 * _gtk_window_set_has_toplevel_focus:
 * @window: a #GtkWindow
 * @has_toplevel_focus: %TRUE if the in
 * 
 * Internal function that sets whether the keyboard focus for the
 * toplevel window (taking into account inter-process embedding.)
 **/
void
_gtk_window_set_has_toplevel_focus (GtkWindow *window,
				   gboolean   has_toplevel_focus)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  has_toplevel_focus = has_toplevel_focus != FALSE;

  if (has_toplevel_focus != priv->has_toplevel_focus)
    {
      priv->has_toplevel_focus = has_toplevel_focus;
      window_update_has_focus (window);

      g_object_notify (G_OBJECT (window), "has-toplevel-focus");
    }
}

/**
 * gtk_window_set_auto_startup_notification:
 * @setting: %TRUE to automatically do startup notification
 *
 * By default, after showing the first #GtkWindow, GTK+ calls 
 * gdk_notify_startup_complete().  Call this function to disable 
 * the automatic startup notification. You might do this if your 
 * first window is a splash screen, and you want to delay notification 
 * until after your real main window has been shown, for example.
 *
 * In that example, you would disable startup notification
 * temporarily, show your splash screen, then re-enable it so that
 * showing the main window would automatically result in notification.
 * 
 * Since: 2.2
 **/
void
gtk_window_set_auto_startup_notification (gboolean setting)
{
  disable_startup_notification = !setting;
}

/**
 * gtk_window_get_window_type:
 * @window: a #GtkWindow
 *
 * Gets the type of the window. See #GtkWindowType.
 *
 * Return value: the type of the window
 *
 * Since: 2.20
 **/
GtkWindowType
gtk_window_get_window_type (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), GTK_WINDOW_TOPLEVEL);

  return window->priv->type;
}

/**
 * gtk_window_get_mnemonics_visible:
 * @window: a #GtkWindow
 *
 * Gets the value of the #GtkWindow:mnemonics-visible property.
 *
 * Returns: %TRUE if mnemonics are supposed to be visible
 * in this window.
 *
 * Since: 2.20
 */
gboolean
gtk_window_get_mnemonics_visible (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->mnemonics_visible;
}

/**
 * gtk_window_set_mnemonics_visible:
 * @window: a #GtkWindow
 * @setting: the new value
 *
 * Sets the #GtkWindow:mnemonics-visible property.
 *
 * Since: 2.20
 */
void
gtk_window_set_mnemonics_visible (GtkWindow *window,
                                  gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->mnemonics_visible != setting)
    {
      priv->mnemonics_visible = setting;
      g_object_notify (G_OBJECT (window), "mnemonics-visible");
    }

  if (priv->auto_mnemonics_timeout_id)
    {
      g_source_remove (priv->auto_mnemonics_timeout_id);
      priv->auto_mnemonics_timeout_id = 0;
    }

  priv->mnemonics_visible_set = TRUE;
}

static gboolean
set_auto_mnemonics_visible_cb (gpointer data)
{
  GtkWindow *window = data;

  gtk_window_set_mnemonics_visible (window, TRUE);

  window->priv->auto_mnemonics_timeout_id = 0;

  return FALSE;
}

void
_gtk_window_set_auto_mnemonics_visible (GtkWindow *window)
{
  g_return_if_fail (GTK_IS_WINDOW (window));

  if (window->priv->auto_mnemonics_timeout_id)
    return;

  window->priv->auto_mnemonics_timeout_id =
    gdk_threads_add_timeout (AUTO_MNEMONICS_DELAY, set_auto_mnemonics_visible_cb, window);
}

/**
 * gtk_window_get_focus_visible:
 * @window: a #GtkWindow
 *
 * Gets the value of the #GtkWindow:focus-visible property.
 *
 * Returns: %TRUE if 'focus rectangles' are supposed to be visible
 *     in this window.
 *
 * Since: 3.2
 */
gboolean
gtk_window_get_focus_visible (GtkWindow *window)
{
  g_return_val_if_fail (GTK_IS_WINDOW (window), FALSE);

  return window->priv->focus_visible;
}

/**
 * gtk_window_set_focus_visible:
 * @window: a #GtkWindow
 * @setting: the new value
 *
 * Sets the #GtkWindow:focus-visible property.
 *
 * Since: 3.2
 */
void
gtk_window_set_focus_visible (GtkWindow *window,
                              gboolean   setting)
{
  GtkWindowPrivate *priv;

  g_return_if_fail (GTK_IS_WINDOW (window));

  priv = window->priv;

  setting = setting != FALSE;

  if (priv->focus_visible != setting)
    {
      priv->focus_visible = setting;
      g_object_notify (G_OBJECT (window), "focus-visible");
    }
}

void
_gtk_window_get_wmclass (GtkWindow  *window,
                         gchar     **wmclass_name,
                         gchar     **wmclass_class)
{
  GtkWindowPrivate *priv = window->priv;

  *wmclass_name = priv->wmclass_name;
  *wmclass_class = priv->wmclass_class;
}

/**
 * gtk_window_set_has_user_ref_count:
 * @window: a #GtkWindow
 * @setting: the new value
 *
 * Tells GTK+ whether to drop its extra reference to the window
 * when gtk_window_destroy() is called.
 *
 * This function is only exported for the benefit of language
 * bindings which may need to keep the window alive until their
 * wrapper object is garbage collected. There is no justification
 * for ever calling this function in an application.
 *
 * Since: 3.0
 */
void
gtk_window_set_has_user_ref_count (GtkWindow *window,
                                   gboolean   setting)
{
  g_return_if_fail (GTK_IS_WINDOW (window));

  window->priv->has_user_ref_count = setting;
}

static void
ensure_state_flag_backdrop (GtkWidget *widget)
{
  GdkWindow *window;
  gboolean window_focused = TRUE;

  window = gtk_widget_get_window (widget);

  window_focused = gdk_window_get_state (window) & GDK_WINDOW_STATE_FOCUSED;

  if (!window_focused)
    gtk_widget_set_state_flags (widget, GTK_STATE_FLAG_BACKDROP, FALSE);
  else
    gtk_widget_unset_state_flags (widget, GTK_STATE_FLAG_BACKDROP);

  gtk_widget_queue_draw (widget);
}
