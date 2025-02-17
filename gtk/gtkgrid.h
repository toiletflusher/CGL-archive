/* GTK - The GIMP Toolkit
 * Copyright (C) 2010 Red Hat, Inc.
 * Author: Matthias Clasen
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

#ifndef __GTK_GRID_H__
#define __GTK_GRID_H__


#if !defined (__GTK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gtk/gtk.h> can be included directly."
#endif

#include <gtk/gtkcontainer.h>


G_BEGIN_DECLS

#define GTK_TYPE_GRID                   (gtk_grid_get_type ())
#define GTK_GRID(obj)                   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_GRID, GtkGrid))
#define GTK_GRID_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_GRID, GtkGridClass))
#define GTK_IS_GRID(obj)                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_GRID))
#define GTK_IS_GRID_CLASS(klass)        (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_GRID))
#define GTK_GRID_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_GRID, GtkGridClass))


typedef struct _GtkGrid              GtkGrid;
typedef struct _GtkGridPrivate       GtkGridPrivate;
typedef struct _GtkGridClass         GtkGridClass;

struct _GtkGrid
{
  /*< private >*/
  GtkContainer container;

  GtkGridPrivate *priv;
};

/**
 * GtkGridClass:
 * @parent_class: The parent class.
 */
struct _GtkGridClass
{
  GtkContainerClass parent_class;

  /*< private >*/

  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
  void (*_gtk_reserved5) (void);
  void (*_gtk_reserved6) (void);
  void (*_gtk_reserved7) (void);
  void (*_gtk_reserved8) (void);
};

GType      gtk_grid_get_type               (void) G_GNUC_CONST;
GtkWidget* gtk_grid_new                    (void);
void       gtk_grid_attach                 (GtkGrid         *grid,
                                            GtkWidget       *child,
                                            gint             left,
                                            gint             top,
                                            gint             width,
                                            gint             height);
void       gtk_grid_attach_next_to         (GtkGrid         *grid,
                                            GtkWidget       *child,
                                            GtkWidget       *sibling,
                                            GtkPositionType  side,
                                            gint             width,
                                            gint             height);
GtkWidget *gtk_grid_get_child_at           (GtkGrid         *grid,
                                            gint             left,
                                            gint             top);
void       gtk_grid_insert_row             (GtkGrid         *grid,
                                            gint             position);
void       gtk_grid_insert_column          (GtkGrid         *grid,
                                            gint             position);
void       gtk_grid_remove_row             (GtkGrid         *grid,
                                           gint             position);
void       gtk_grid_remove_column          (GtkGrid         *grid,
                                            gint             position);
void       gtk_grid_insert_next_to         (GtkGrid         *grid,
                                            GtkWidget       *sibling,
                                            GtkPositionType  side);
void       gtk_grid_set_row_homogeneous    (GtkGrid         *grid,
                                            gboolean         homogeneous);
gboolean   gtk_grid_get_row_homogeneous    (GtkGrid         *grid);
void       gtk_grid_set_row_spacing        (GtkGrid         *grid,
                                            guint            spacing);
guint      gtk_grid_get_row_spacing        (GtkGrid         *grid);
void       gtk_grid_set_column_homogeneous (GtkGrid         *grid,
                                            gboolean         homogeneous);
gboolean   gtk_grid_get_column_homogeneous (GtkGrid         *grid);
void       gtk_grid_set_column_spacing     (GtkGrid         *grid,
                                            guint            spacing);
guint      gtk_grid_get_column_spacing     (GtkGrid         *grid);


G_END_DECLS

#endif /* __GTK_GRID_H__ */
