/*
 * Copyright © 2011 William Hua, Ryan Lortie
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the licence, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: William Hua <william@attente.ca>
 *         Ryan Lortie <desrt@desrt.ca>
 */

#include "config.h"

#include "gtkapplicationprivate.h"
#include "gtkmenutracker.h"
#include "gtkicontheme.h"
#include "gtktoolbar.h"
#include "gtkquartz.h"

#include <gdk/quartz/gdkquartz.h>

#import <Cocoa/Cocoa.h>

#define ICON_SIZE 16

#define BLACK               "#000000"
#define TANGO_CHAMELEON_3   "#4e9a06"
#define TANGO_ORANGE_2      "#f57900"
#define TANGO_SCARLET_RED_2 "#cc0000"

@interface GNSMenu : NSMenu
{
  GtkMenuTracker *tracker;
}

- (id)initWithTitle:(NSString *)title model:(GMenuModel *)model observable:(GtkActionObservable *)observable;

- (id)initWithTitle:(NSString *)title trackerItem:(GtkMenuTrackerItem *)trackerItem;

@end

@interface NSMenuItem (GtkMenuTrackerItem)

+ (id)menuItemForTrackerItem:(GtkMenuTrackerItem *)trackerItem;

@end

@interface GNSMenuItem : NSMenuItem
{
  GtkMenuTrackerItem *trackerItem;
  gulong trackerItemChangedHandler;
  GCancellable *cancellable;
}

- (id)initWithTrackerItem:(GtkMenuTrackerItem *)aTrackerItem;

- (void)didChangeLabel;
- (void)didChangeIcon;
- (void)didChangeSensitive;
- (void)didChangeVisible;
- (void)didChangeToggled;
- (void)didChangeAccel;

- (void)didSelectItem:(id)sender;

@end

static void
tracker_item_changed (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    user_data)
{
  GNSMenuItem *item = user_data;
  const gchar *name = g_param_spec_get_name (pspec);

  if (name != NULL)
    {
      if (g_str_equal (name, "label"))
        [item didChangeLabel];
      else if (g_str_equal (name, "icon"))
        [item didChangeIcon];
      else if (g_str_equal (name, "visible"))
        [item didChangeVisible];
      else if (g_str_equal (name, "toggled"))
        [item didChangeToggled];
      else if (g_str_equal (name, "accel"))
        [item didChangeAccel];
    }
}

static void
icon_loaded (GObject      *object,
             GAsyncResult *result,
             gpointer      user_data)
{
  GtkIconInfo *info = GTK_ICON_INFO (object);
  GNSMenuItem *item = user_data;
  GError *error = NULL;
  GdkPixbuf *pixbuf;

  pixbuf = gtk_icon_info_load_symbolic_finish (info, result, NULL, &error);

  if (pixbuf != NULL)
    {
      [item setImage:_gtk_quartz_create_image_from_pixbuf (pixbuf)];
      g_object_unref (pixbuf);
    }
  else
    {
      /* on failure to load, clear the old icon */
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        [item setImage:nil];

      g_error_free (error);
    }
}

@implementation GNSMenuItem

- (id)initWithTrackerItem:(GtkMenuTrackerItem *)aTrackerItem
{
  self = [super initWithTitle:@""
                       action:@selector(didSelectItem:)
                keyEquivalent:@""];

  if (self != nil)
    {
      [self setTarget:self];

      trackerItem = g_object_ref (aTrackerItem);
      trackerItemChangedHandler = g_signal_connect (trackerItem, "notify", G_CALLBACK (tracker_item_changed), self);

      [self didChangeLabel];
      [self didChangeIcon];
      [self didChangeSensitive];
      [self didChangeVisible];
      [self didChangeToggled];
      [self didChangeAccel];

      if (gtk_menu_tracker_item_get_has_submenu (trackerItem))
        [self setSubmenu:[[[GNSMenu alloc] initWithTitle:[self title] trackerItem:trackerItem] autorelease]];
    }

  return self;
}

- (void)dealloc
{
  if (cancellable != NULL)
    {
      g_cancellable_cancel (cancellable);
      g_clear_object (&cancellable);
    }

  g_signal_handler_disconnect (trackerItem, trackerItemChangedHandler);
  g_object_unref (trackerItem);

  [super dealloc];
}

- (void)didChangeLabel
{
  gchar *label = _gtk_toolbar_elide_underscores (gtk_menu_tracker_item_get_label (trackerItem));

  [self setTitle:[NSString stringWithUTF8String:label ? : ""]];

  g_free (label);
}

- (void)didChangeIcon
{
  GIcon *icon = gtk_menu_tracker_item_get_icon (trackerItem);

  if (cancellable != NULL)
    {
      g_cancellable_cancel (cancellable);
      g_clear_object (&cancellable);
    }

  if (icon != NULL)
    {
      static gboolean parsed;

      static GdkRGBA foreground;
      static GdkRGBA success;
      static GdkRGBA warning;
      static GdkRGBA error;

      GtkIconTheme *theme;
      GtkIconInfo *info;
      gint scale;

      if (!parsed)
        {
          gdk_rgba_parse (&foreground, BLACK);
          gdk_rgba_parse (&success, TANGO_CHAMELEON_3);
          gdk_rgba_parse (&warning, TANGO_ORANGE_2);
          gdk_rgba_parse (&error, TANGO_SCARLET_RED_2);

          parsed = TRUE;
        }

      theme = gtk_icon_theme_get_default ();
      scale = roundf ([[NSScreen mainScreen] backingScaleFactor]);
      info = gtk_icon_theme_lookup_by_gicon_for_scale (theme, icon, ICON_SIZE, scale, GTK_ICON_LOOKUP_USE_BUILTIN);

      if (info != NULL)
        {
          cancellable = g_cancellable_new ();
          gtk_icon_info_load_symbolic_async (info, &foreground, &success, &warning, &error,
                                             cancellable, icon_loaded, self);
          g_object_unref (info);
          return;
        }
    }

  [self setImage:nil];
}

- (void)didChangeSensitive
{
  [self setEnabled:gtk_menu_tracker_item_get_sensitive (trackerItem) ? YES : NO];
}

- (void)didChangeVisible
{
  [self setHidden:gtk_menu_tracker_item_get_visible (trackerItem) ? NO : YES];
}

- (void)didChangeToggled
{
  [self setState:gtk_menu_tracker_item_get_toggled (trackerItem) ? NSOnState : NSOffState];
}

- (void)didChangeAccel
{
  const gchar *accel = gtk_menu_tracker_item_get_accel (trackerItem);

  if (accel != NULL)
    {
      guint key;
      GdkModifierType mask;
      unichar character;
      NSUInteger modifiers;

      gtk_accelerator_parse (accel, &key, &mask);

      character = gdk_quartz_get_key_equivalent (key);
      [self setKeyEquivalent:[NSString stringWithCharacters:&character length:1]];

      modifiers = 0;
      if (mask & GDK_SHIFT_MASK)
        modifiers |= NSShiftKeyMask;
      if (mask & GDK_CONTROL_MASK)
        modifiers |= NSControlKeyMask;
      if (mask & GDK_MOD1_MASK)
        modifiers |= NSAlternateKeyMask;
      if (mask & GDK_META_MASK)
        modifiers |= NSCommandKeyMask;
      [self setKeyEquivalentModifierMask:modifiers];
    }
  else
    {
      [self setKeyEquivalent:@""];
      [self setKeyEquivalentModifierMask:0];
    }
}

- (void)didSelectItem:(id)sender
{
  gtk_menu_tracker_item_activated (trackerItem);
}

@end

@implementation NSMenuItem (GtkMenuTrackerItem)

+ (id)menuItemForTrackerItem:(GtkMenuTrackerItem *)trackerItem
{
  if (gtk_menu_tracker_item_get_is_separator (trackerItem))
    return [NSMenuItem separatorItem];

  return [[[GNSMenuItem alloc] initWithTrackerItem:trackerItem] autorelease];
}

@end

static void
menu_item_inserted (GtkMenuTrackerItem *item,
                    gint                position,
                    gpointer            user_data)
{
  GNSMenu *menu = user_data;

  [menu insertItem:[NSMenuItem menuItemForTrackerItem:item] atIndex:position];
}

static void
menu_item_removed (gint     position,
                   gpointer user_data)
{
  GNSMenu *menu = user_data;

  [menu removeItemAtIndex:position];
}

@implementation GNSMenu

- (id)initWithTitle:(NSString *)title model:(GMenuModel *)model observable:(GtkActionObservable *)observable
{
  self = [super initWithTitle:title];

  if (self != nil)
    {
      [self setAutoenablesItems:NO];

      tracker = gtk_menu_tracker_new (observable,
                                      model,
                                      NO,
                                      NULL,
                                      menu_item_inserted,
                                      menu_item_removed,
                                      self);
    }

  return self;
}

- (id)initWithTitle:(NSString *)title trackerItem:(GtkMenuTrackerItem *)trackerItem
{
  self = [super initWithTitle:title];

  if (self != nil)
    {
      [self setAutoenablesItems:NO];

      tracker = gtk_menu_tracker_new_for_item_submenu (trackerItem,
                                                       menu_item_inserted,
                                                       menu_item_removed,
                                                       self);
    }

  return self;
}

- (void)dealloc
{
  gtk_menu_tracker_free (tracker);

  [super dealloc];
}

@end

void
gtk_application_impl_quartz_setup_menu (GMenuModel     *model,
                                        GtkActionMuxer *muxer)
{
  NSMenu *menu;

  if (model != NULL)
    menu = [[GNSMenu alloc] initWithTitle:@"Main Menu" model:model observable:GTK_ACTION_OBSERVABLE (muxer)];
  else
    menu = [[NSMenu alloc] init];

  [NSApp setMainMenu:menu];
  [menu release];
}
