/* GTK - The GIMP Toolkit
 * Copyright (C) 2000 Red Hat, Inc.
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
#include <string.h>
#include "gtkimcontext.h"
#include "gtkprivate.h"
#include "gtkmarshalers.h"
#include "gtkintl.h"

/**
 * SECTION:gtkimcontext
 * @title: GtkIMContext
 * @short_description: Base class for input method contexts
 * @include: gtk/gtk.h,gtk/gtkimmodule.h
 *
 * #GtkIMContext defines the interface for GTK+ input methods. An input method
 * is used by GTK+ text input widgets like #GtkEntry to map from key events to
 * Unicode character strings.
 *
 * The user may change the current input method via a context menu, unless the   
 * #GtkSettings:gtk-show-input-method-menu GtkSettings property is set to FALSE. 
 * The default input method can be set programmatically via the 
 * #GtkSettings:gtk-im-module GtkSettings property. Alternatively, you may set 
 * the GTK_IM_MODULE environment variable as documented in #gtk-running.
 *
 * The #GtkEntry #GtkEntry:im-module and #GtkTextView #GtkTextView:im-module 
 * properties may also be used to set input methods for specific widget 
 * instances. For instance, a certain entry widget might be expected to contain 
 * certain characters which would be easier to input with a certain input 
 * method.
 *
 * An input method may consume multiple key events in sequence and finally
 * output the composed result. This is called preediting, and an input method
 * may provide feedback about this process by displaying the intermediate
 * composition states as preedit text. For instance, the default GTK+ input
 * method implements the input of arbitrary Unicode code points by holding down
 * the Control and Shift keys and then typing "U" followed by the hexadecimal
 * digits of the code point.  When releasing the Control and Shift keys,
 * preediting ends and the character is inserted as text. Ctrl+Shift+u20AC for
 * example results in the € sign.
 *
 * Additional input methods can be made available for use by GTK+ widgets as
 * loadable modules. An input method module is a small shared library which
 * implements a subclass of #GtkIMContext or #GtkIMContextSimple and exports
 * these four functions:
 *
 * <informalexample><programlisting>
 * void im_module_init(#GTypeModule *module);
 * </programlisting></informalexample>
 * This function should register the #GType of the #GtkIMContext subclass which
 * implements the input method by means of g_type_module_register_type(). Note
 * that g_type_register_static() cannot be used as the type needs to be
 * registered dynamically.
 *
 * <informalexample><programlisting>
 * void im_module_exit(void);
 * </programlisting></informalexample>
 * Here goes any cleanup code your input method might require on module unload.
 *
 * <informalexample><programlisting>
 * void im_module_list(const #GtkIMContextInfo ***contexts, int *n_contexts)
 * {
 *   *contexts = info_list;
 *   *n_contexts = G_N_ELEMENTS (info_list);
 * }
 * </programlisting></informalexample>
 * This function returns the list of input methods provided by the module. The
 * example implementation above shows a common solution and simply returns a
 * pointer to statically defined array of #GtkIMContextInfo items for each
 * provided input method.
 *
 * <informalexample><programlisting>
 * #GtkIMContext * im_module_create(const #gchar *context_id);
 * </programlisting></informalexample>
 * This function should return a pointer to a newly created instance of the
 * #GtkIMContext subclass identified by @context_id. The context ID is the same
 * as specified in the #GtkIMContextInfo array returned by im_module_list().
 *
 * After a new loadable input method module has been installed on the system,
 * the configuration file <filename>gtk.immodules</filename> needs to be
 * regenerated by <link linkend="gtk-query-immodules-3.0">gtk-query-immodules-3.0</link>,
 * in order for the new input method to become available to GTK+ applications.
 */

enum {
  PREEDIT_START,
  PREEDIT_END,
  PREEDIT_CHANGED,
  COMMIT,
  RETRIEVE_SURROUNDING,
  DELETE_SURROUNDING,
  LAST_SIGNAL
};

static guint im_context_signals[LAST_SIGNAL] = { 0 };

static void     gtk_im_context_real_get_preedit_string (GtkIMContext   *context,
							gchar         **str,
							PangoAttrList **attrs,
							gint           *cursor_pos);
static gboolean gtk_im_context_real_filter_keypress    (GtkIMContext   *context,
							GdkEventKey    *event);
static gboolean gtk_im_context_real_get_surrounding    (GtkIMContext   *context,
							gchar         **text,
							gint           *cursor_index);
static void     gtk_im_context_real_set_surrounding    (GtkIMContext   *context,
							const char     *text,
							gint            len,
							gint            cursor_index);

G_DEFINE_ABSTRACT_TYPE (GtkIMContext, gtk_im_context, G_TYPE_OBJECT)

/**
 * GtkIMContextClass:
 * @preedit_start: Default handler of the #GtkIMContext::preedit-start signal.
 * @preedit_end: Default handler of the #GtkIMContext::preedit-end signal.
 * @preedit_changed: Default handler of the #GtkIMContext::preedit-changed
 *   signal.
 * @commit: Default handler of the #GtkIMContext::commit signal.
 * @retrieve_surrounding: Default handler of the
 *   #GtkIMContext::retrieve-surrounding signal.
 * @delete_surrounding: Default handler of the
 *   #GtkIMContext::delete-surrounding signal.
 * @set_client_window: Called via gtk_im_context_set_client_window() when the
 *   input window where the entered text will appear changes. Override this to
 *   keep track of the current input window, for instance for the purpose of
 *   positioning a status display of your input method.
 * @get_preedit_string: Called via gtk_im_context_get_preedit_string() to
 *   retrieve the text currently being preedited for display at the cursor
 *   position. Any input method which composes complex characters or any
 *   other compositions from multiple sequential key presses should override
 *   this method to provide feedback.
 * @filter_keypress: Called via gtk_im_context_filter_keypress() on every
 *   key press or release event. Every non-trivial input method needs to
 *   override this in order to implement the mapping from key events to text.
 *   A return value of %TRUE indicates to the caller that the event was
 *   consumed by the input method. In that case, the #GtkIMContext::commit
 *   signal should be emitted upon completion of a key sequence to pass the
 *   resulting text back to the input widget. Alternatively, %FALSE may be
 *   returned to indicate that the event wasn't handled by the input method.
 *   If a builtin mapping exists for the key, it is used to produce a
 *   character.
 * @focus_in: Called via gtk_im_context_focus_in() when the input widget
 *   has gained focus. May be overridden to keep track of the current focus.
 * @focus_out: Called via gtk_im_context_focus_out() when the input widget
 *   has lost focus. May be overridden to keep track of the current focus.
 * @reset: Called via gtk_im_context_reset() to signal a change such as a
 *   change in cursor position. An input method that implements preediting
 *   should override this method to clear the preedit state on reset.
 * @set_cursor_location: Called via gtk_im_context_set_cursor_location()
 *   to inform the input method of the current cursor location relative to
 *   the client window. May be overridden to implement the display of popup
 *   windows at the cursor position.
 * @set_use_preedit: Called via gtk_im_context_set_use_preedit() to control
 *   the use of the preedit string. Override this to display feedback by some
 *   other means if turned off.
 * @set_surrounding: Called via gtk_im_context_set_surrounding() in response
 *   to signal #GtkIMContext::retrieve-surrounding to update the input
 *   method's idea of the context around the cursor. It is not necessary to
 *   override this method even with input methods which implement
 *   context-dependent behavior. The base implementation is sufficient for
 *   gtk_im_context_get_surrounding() to work.
 * @get_surrounding: Called via gtk_im_context_get_surrounding() to update
 *   the context around the cursor location. It is not necessary to override
 *   this method even with input methods which implement context-dependent
 *   behavior. The base implementation emits
 *   #GtkIMContext::retrieve-surrounding and records the context received
 *   by the subsequent invocation of @get_surrounding.
 */
static void
gtk_im_context_class_init (GtkIMContextClass *klass)
{
  klass->get_preedit_string = gtk_im_context_real_get_preedit_string;
  klass->filter_keypress = gtk_im_context_real_filter_keypress;
  klass->get_surrounding = gtk_im_context_real_get_surrounding;
  klass->set_surrounding = gtk_im_context_real_set_surrounding;

  /**
   * GtkIMContext::preedit-start:
   * @context: the object on which the signal is emitted
   *
   * The ::preedit-start signal is emitted when a new preediting sequence
   * starts.
   */
  im_context_signals[PREEDIT_START] =
    g_signal_new (I_("preedit-start"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GtkIMContextClass, preedit_start),
		  NULL, NULL,
		  _gtk_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  /**
   * GtkIMContext::preedit-end:
   * @context: the object on which the signal is emitted
   *
   * The ::preedit-end signal is emitted when a preediting sequence
   * has been completed or canceled.
   */
  im_context_signals[PREEDIT_END] =
    g_signal_new (I_("preedit-end"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GtkIMContextClass, preedit_end),
		  NULL, NULL,
		  _gtk_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  /**
   * GtkIMContext::preedit-changed:
   * @context: the object on which the signal is emitted
   *
   * The ::preedit-changed signal is emitted whenever the preedit sequence
   * currently being entered has changed.  It is also emitted at the end of
   * a preedit sequence, in which case
   * gtk_im_context_get_preedit_string() returns the empty string.
   */
  im_context_signals[PREEDIT_CHANGED] =
    g_signal_new (I_("preedit-changed"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GtkIMContextClass, preedit_changed),
		  NULL, NULL,
		  _gtk_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
  /**
   * GtkIMContext::commit:
   * @context: the object on which the signal is emitted
   * @str: the completed character(s) entered by the user
   *
   * The ::commit signal is emitted when a complete input sequence
   * has been entered by the user. This can be a single character
   * immediately after a key press or the final result of preediting.
   */
  im_context_signals[COMMIT] =
    g_signal_new (I_("commit"),
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (GtkIMContextClass, commit),
		  NULL, NULL,
		  _gtk_marshal_VOID__STRING,
		  G_TYPE_NONE, 1,
		  G_TYPE_STRING);
  /**
   * GtkIMContext::retrieve-surrounding:
   * @context: the object on which the signal is emitted
   *
   * The ::retrieve-surrounding signal is emitted when the input method
   * requires the context surrounding the cursor.  The callback should set
   * the input method surrounding context by calling the
   * gtk_im_context_set_surrounding() method.
   *
   * Return value: %TRUE if the signal was handled.
   */
  im_context_signals[RETRIEVE_SURROUNDING] =
    g_signal_new (I_("retrieve-surrounding"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtkIMContextClass, retrieve_surrounding),
                  _gtk_boolean_handled_accumulator, NULL,
                  _gtk_marshal_BOOLEAN__VOID,
                  G_TYPE_BOOLEAN, 0);
  /**
   * GtkIMContext::delete-surrounding:
   * @context: the object on which the signal is emitted
   * @offset:  the character offset from the cursor position of the text
   *           to be deleted. A negative value indicates a position before
   *           the cursor.
   * @n_chars: the number of characters to be deleted
   *
   * The ::delete-surrounding signal is emitted when the input method
   * needs to delete all or part of the context surrounding the cursor.
   *
   * Return value: %TRUE if the signal was handled.
   */
  im_context_signals[DELETE_SURROUNDING] =
    g_signal_new (I_("delete-surrounding"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtkIMContextClass, delete_surrounding),
                  _gtk_boolean_handled_accumulator, NULL,
                  _gtk_marshal_BOOLEAN__INT_INT,
                  G_TYPE_BOOLEAN, 2,
                  G_TYPE_INT,
		  G_TYPE_INT);
}

static void
gtk_im_context_init (GtkIMContext *im_context)
{
}

static void
gtk_im_context_real_get_preedit_string (GtkIMContext       *context,
					gchar             **str,
					PangoAttrList     **attrs,
					gint               *cursor_pos)
{
  if (str)
    *str = g_strdup ("");
  if (attrs)
    *attrs = pango_attr_list_new ();
  if (cursor_pos)
    *cursor_pos = 0;
}

static gboolean
gtk_im_context_real_filter_keypress (GtkIMContext       *context,
				     GdkEventKey        *event)
{
  return FALSE;
}

typedef struct
{
  gchar *text;
  gint cursor_index;
} SurroundingInfo;

static void
gtk_im_context_real_set_surrounding (GtkIMContext  *context,
				     const gchar   *text,
				     gint           len,
				     gint           cursor_index)
{
  SurroundingInfo *info = g_object_get_data (G_OBJECT (context),
                                             "gtk-im-surrounding-info");

  if (info)
    {
      g_free (info->text);
      info->text = g_strndup (text, len);
      info->cursor_index = cursor_index;
    }
}

static gboolean
gtk_im_context_real_get_surrounding (GtkIMContext *context,
				     gchar       **text,
				     gint         *cursor_index)
{
  gboolean result;
  gboolean info_is_local = FALSE;
  SurroundingInfo local_info = { NULL, 0 };
  SurroundingInfo *info;
  
  info = g_object_get_data (G_OBJECT (context), "gtk-im-surrounding-info");
  if (!info)
    {
      info = &local_info;
      g_object_set_data (G_OBJECT (context), I_("gtk-im-surrounding-info"), info);
      info_is_local = TRUE;
    }
  
  g_signal_emit (context,
		 im_context_signals[RETRIEVE_SURROUNDING], 0,
		 &result);

  if (result)
    {
      *text = g_strdup (info->text ? info->text : "");
      *cursor_index = info->cursor_index;
    }
  else
    {
      *text = NULL;
      *cursor_index = 0;
    }

  if (info_is_local)
    {
      g_free (info->text);
      g_object_set_data (G_OBJECT (context), I_("gtk-im-surrounding-info"), NULL);
    }
  
  return result;
}

/**
 * gtk_im_context_set_client_window:
 * @context: a #GtkIMContext
 * @window: (allow-none):  the client window. This may be %NULL to indicate
 *           that the previous client window no longer exists.
 * 
 * Set the client window for the input context; this is the
 * #GdkWindow in which the input appears. This window is
 * used in order to correctly position status windows, and may
 * also be used for purposes internal to the input method.
 **/
void
gtk_im_context_set_client_window (GtkIMContext *context,
				  GdkWindow    *window)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->set_client_window)
    klass->set_client_window (context, window);
}

/**
 * gtk_im_context_get_preedit_string:
 * @context:    a #GtkIMContext
 * @str:        (out) (transfer full): location to store the retrieved
 *              string. The string retrieved must be freed with g_free().
 * @attrs:      (out) (transfer full): location to store the retrieved
 *              attribute list.  When you are done with this list, you
 *              must unreference it with pango_attr_list_unref().
 * @cursor_pos: (out): location to store position of cursor (in characters)
 *              within the preedit string.  
 * 
 * Retrieve the current preedit string for the input context,
 * and a list of attributes to apply to the string.
 * This string should be displayed inserted at the insertion
 * point.
 **/
void
gtk_im_context_get_preedit_string (GtkIMContext   *context,
				   gchar         **str,
				   PangoAttrList **attrs,
				   gint           *cursor_pos)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  
  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  klass->get_preedit_string (context, str, attrs, cursor_pos);
  g_return_if_fail (str == NULL || g_utf8_validate (*str, -1, NULL));
}

/**
 * gtk_im_context_filter_keypress:
 * @context: a #GtkIMContext
 * @event: the key event
 * 
 * Allow an input method to internally handle key press and release 
 * events. If this function returns %TRUE, then no further processing
 * should be done for this key event.
 * 
 * Return value: %TRUE if the input method handled the key event.
 *
 **/
gboolean
gtk_im_context_filter_keypress (GtkIMContext *context,
				GdkEventKey  *key)
{
  GtkIMContextClass *klass;
  
  g_return_val_if_fail (GTK_IS_IM_CONTEXT (context), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  return klass->filter_keypress (context, key);
}

/**
 * gtk_im_context_focus_in:
 * @context: a #GtkIMContext
 *
 * Notify the input method that the widget to which this
 * input context corresponds has gained focus. The input method
 * may, for example, change the displayed feedback to reflect
 * this change.
 **/
void
gtk_im_context_focus_in (GtkIMContext   *context)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  
  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->focus_in)
    klass->focus_in (context);
}

/**
 * gtk_im_context_focus_out:
 * @context: a #GtkIMContext
 *
 * Notify the input method that the widget to which this
 * input context corresponds has lost focus. The input method
 * may, for example, change the displayed feedback or reset the contexts
 * state to reflect this change.
 **/
void
gtk_im_context_focus_out (GtkIMContext   *context)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->focus_out)
    klass->focus_out (context);
}

/**
 * gtk_im_context_reset:
 * @context: a #GtkIMContext
 *
 * Notify the input method that a change such as a change in cursor
 * position has been made. This will typically cause the input
 * method to clear the preedit state.
 **/
void
gtk_im_context_reset (GtkIMContext   *context)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->reset)
    klass->reset (context);
}


/**
 * gtk_im_context_set_cursor_location:
 * @context: a #GtkIMContext
 * @area: new location
 *
 * Notify the input method that a change in cursor 
 * position has been made. The location is relative to the client
 * window.
 **/
void
gtk_im_context_set_cursor_location (GtkIMContext       *context,
				    const GdkRectangle *area)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->set_cursor_location)
    klass->set_cursor_location (context, (GdkRectangle *) area);
}

/**
 * gtk_im_context_set_use_preedit:
 * @context: a #GtkIMContext
 * @use_preedit: whether the IM context should use the preedit string.
 * 
 * Sets whether the IM context should use the preedit string
 * to display feedback. If @use_preedit is FALSE (default
 * is TRUE), then the IM context may use some other method to display
 * feedback, such as displaying it in a child of the root window.
 **/
void
gtk_im_context_set_use_preedit (GtkIMContext *context,
				gboolean      use_preedit)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->set_use_preedit)
    klass->set_use_preedit (context, use_preedit);
}

/**
 * gtk_im_context_set_surrounding:
 * @context: a #GtkIMContext 
 * @text: text surrounding the insertion point, as UTF-8.
 *        the preedit string should not be included within
 *        @text.
 * @len: the length of @text, or -1 if @text is nul-terminated
 * @cursor_index: the byte index of the insertion cursor within @text.
 * 
 * Sets surrounding context around the insertion point and preedit
 * string. This function is expected to be called in response to the
 * GtkIMContext::retrieve_surrounding signal, and will likely have no
 * effect if called at other times.
 **/
void
gtk_im_context_set_surrounding (GtkIMContext  *context,
				const gchar   *text,
				gint           len,
				gint           cursor_index)
{
  GtkIMContextClass *klass;
  
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  g_return_if_fail (text != NULL || len == 0);

  if (text == NULL && len == 0)
    text = "";
  if (len < 0)
    len = strlen (text);

  g_return_if_fail (cursor_index >= 0 && cursor_index <= len);

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->set_surrounding)
    klass->set_surrounding (context, text, len, cursor_index);
}

/**
 * gtk_im_context_get_surrounding:
 * @context: a #GtkIMContext
 * @text: (out) (transfer full): location to store a UTF-8 encoded
 *        string of text holding context around the insertion point.
 *        If the function returns %TRUE, then you must free the result
 *        stored in this location with g_free().
 * @cursor_index: (out) location to store byte index of the insertion
 *        cursor within @text.
 * 
 * Retrieves context around the insertion point. Input methods
 * typically want context in order to constrain input text based on
 * existing text; this is important for languages such as Thai where
 * only some sequences of characters are allowed.
 *
 * This function is implemented by emitting the
 * GtkIMContext::retrieve_surrounding signal on the input method; in
 * response to this signal, a widget should provide as much context as
 * is available, up to an entire paragraph, by calling
 * gtk_im_context_set_surrounding(). Note that there is no obligation
 * for a widget to respond to the ::retrieve_surrounding signal, so input
 * methods must be prepared to function without context.
 *
 * Return value: %TRUE if surrounding text was provided; in this case
 *    you must free the result stored in *text.
 **/
gboolean
gtk_im_context_get_surrounding (GtkIMContext *context,
				gchar       **text,
				gint         *cursor_index)
{
  GtkIMContextClass *klass;
  gchar *local_text = NULL;
  gint local_index;
  gboolean result = FALSE;
  
  g_return_val_if_fail (GTK_IS_IM_CONTEXT (context), FALSE);

  klass = GTK_IM_CONTEXT_GET_CLASS (context);
  if (klass->get_surrounding)
    result = klass->get_surrounding (context,
				     text ? text : &local_text,
				     cursor_index ? cursor_index : &local_index);

  if (result)
    g_free (local_text);

  return result;
}

/**
 * gtk_im_context_delete_surrounding:
 * @context: a #GtkIMContext
 * @offset: offset from cursor position in chars;
 *    a negative value means start before the cursor.
 * @n_chars: number of characters to delete.
 * 
 * Asks the widget that the input context is attached to to delete
 * characters around the cursor position by emitting the
 * GtkIMContext::delete_surrounding signal. Note that @offset and @n_chars
 * are in characters not in bytes which differs from the usage other
 * places in #GtkIMContext.
 *
 * In order to use this function, you should first call
 * gtk_im_context_get_surrounding() to get the current context, and
 * call this function immediately afterwards to make sure that you
 * know what you are deleting. You should also account for the fact
 * that even if the signal was handled, the input context might not
 * have deleted all the characters that were requested to be deleted.
 *
 * This function is used by an input method that wants to make
 * subsitutions in the existing text in response to new input. It is
 * not useful for applications.
 * 
 * Return value: %TRUE if the signal was handled.
 **/
gboolean
gtk_im_context_delete_surrounding (GtkIMContext *context,
				   gint          offset,
				   gint          n_chars)
{
  gboolean result;
  
  g_return_val_if_fail (GTK_IS_IM_CONTEXT (context), FALSE);

  g_signal_emit (context,
		 im_context_signals[DELETE_SURROUNDING], 0,
		 offset, n_chars, &result);

  return result;
}
