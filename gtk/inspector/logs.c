/*
 * Copyright (c) 2018, Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "logs.h"

#include "gtktextview.h"
#include "gtkmessagedialog.h"
#include "gtkfilechooserdialog.h"
#include "gtktogglebutton.h"
#include "gtklabel.h"
#include "gtktooltip.h"
#include "gtktextiter.h"
#include "gdkinternals.h"


struct _GtkInspectorLogsPrivate
{
  GtkWidget *view;
  GtkTextBuffer *text;
  GtkToggleButton *disable_button;
  GtkWidget *events;
  GtkWidget *misc;
  GtkWidget *dnd;
  GtkWidget *input;
  GtkWidget *cursor;
  GtkWidget *eventloop;
  GtkWidget *frames;
  GtkWidget *settings;
  GtkWidget *opengl;
  GtkWidget *vulkan;
  GtkWidget *selection;
  GtkWidget *clipboard;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtkInspectorLogs, gtk_inspector_logs, GTK_TYPE_BOX)

#define LEVELS (G_LOG_LEVEL_INFO | G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_DEBUG)

static GLogWriterOutput
inspector_log_writer (GLogLevelFlags   log_level,
                      const GLogField *fields,
                      gsize            n_fields,
                      gpointer         user_data)
{
  GtkInspectorLogs *logs = user_data;

  if (log_level & LEVELS)
    {
      const char *log_domain = NULL;
      int i;

      for (i = 0; i < n_fields; i++)
        {
          if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0)
            {
              log_domain = fields[i].value;
              break;
            }
        }

      if (g_strcmp0 (log_domain, "Gdk") == 0 ||
          g_strcmp0 (log_domain, "Gsk") == 0 ||
          g_strcmp0 (log_domain, "Gtk") == 0)
        {
          char *message;
          GtkTextIter iter;
          GtkTextMark *mark;

          message = g_log_writer_format_fields (log_level, fields, n_fields, FALSE);

          gtk_text_buffer_get_end_iter (logs->priv->text, &iter);
          gtk_text_buffer_insert (logs->priv->text, &iter, "\n", 1);
          gtk_text_buffer_insert (logs->priv->text, &iter, message, -1);

          gtk_text_iter_set_line_offset (&iter, 0);
          mark = gtk_text_buffer_get_mark (logs->priv->text, "scroll");
          gtk_text_buffer_move_mark (logs->priv->text, mark, &iter);
          gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (logs->priv->view), mark);

          g_free (message);

          return G_LOG_WRITER_HANDLED;
        }
    }

  return g_log_writer_default (log_level, fields, n_fields, NULL);
}

static void
disable_toggled (GtkToggleButton  *button,
                 GtkInspectorLogs *logs)
{
  if (gtk_toggle_button_get_active (button))
    g_log_set_writer_func (inspector_log_writer, logs, NULL);
  else
    g_log_set_writer_func (g_log_writer_default, NULL, NULL);
}

static gchar *
get_current_text (GtkTextBuffer *buffer)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

  return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
save_to_file (GtkInspectorLogs *logs,
              const gchar      *filename)
{
  gchar *text;
  GError *error = NULL;

  text = get_current_text (logs->priv->text);

  if (!g_file_set_contents (filename, text, -1, &error))
    {
      GtkWidget *dialog;

      dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (logs))),
                                       GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_INFO,
                                       GTK_BUTTONS_OK,
                                       _("Saving logs failed"));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                "%s", error->message);
      g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
      gtk_widget_show (dialog);
      g_error_free (error);
    }

  g_free (text);
}

static void
save_response (GtkWidget        *dialog,
               gint              response,
               GtkInspectorLogs *logs)
{
  gtk_widget_hide (dialog);

  if (response == GTK_RESPONSE_ACCEPT)
    {
      gchar *filename;

      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      save_to_file (logs, filename);
      g_free (filename);
    }

  gtk_widget_destroy (dialog);
}

static void
save_clicked (GtkButton        *button,
              GtkInspectorLogs *logs)
{
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new ("",
                                        GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (logs))),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("_Cancel"), GTK_RESPONSE_CANCEL,
                                        _("_Save"), GTK_RESPONSE_ACCEPT,
                                        NULL);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), "logs.txt");
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
  g_signal_connect (dialog, "response", G_CALLBACK (save_response), logs);
  gtk_widget_show (dialog);
}

static void
clear_clicked (GtkButton        *button,
               GtkInspectorLogs *logs)
{
  gtk_text_buffer_set_text (logs->priv->text, "", 0);
}

static void
gtk_inspector_logs_init (GtkInspectorLogs *logs)
{
  GtkTextIter iter;

  logs->priv = gtk_inspector_logs_get_instance_private (logs);
  gtk_widget_init_template (GTK_WIDGET (logs));
  gtk_text_buffer_get_end_iter (logs->priv->text, &iter);
  gtk_text_buffer_create_mark (logs->priv->text, "scroll", &iter, TRUE);
  disable_toggled (logs->priv->disable_button, logs);
}

static void
finalize (GObject *object)
{
  //GtkInspectorLogs *logs = GTK_INSPECTOR_LOGS (object);

  G_OBJECT_CLASS (gtk_inspector_logs_parent_class)->finalize (object);
}

static void
flag_toggled (GtkWidget        *button,
              GtkInspectorLogs *logs)
{
  GdkDebugFlags flags;

  flags = 0;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->events)))
    flags = flags | GDK_DEBUG_EVENTS;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->misc)))
    flags = flags | GDK_DEBUG_MISC;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->dnd)))
    flags = flags | GDK_DEBUG_DND;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->input)))
    flags = flags | GDK_DEBUG_INPUT;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->cursor)))
    flags = flags | GDK_DEBUG_CURSOR;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->eventloop)))
    flags = flags | GDK_DEBUG_EVENTLOOP;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->frames)))
    flags = flags | GDK_DEBUG_FRAMES;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->settings)))
    flags = flags | GDK_DEBUG_SETTINGS;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->opengl)))
    flags = flags | GDK_DEBUG_OPENGL;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->vulkan)))
    flags = flags | GDK_DEBUG_VULKAN;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->selection)))
    flags = flags | GDK_DEBUG_SELECTION;
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (logs->priv->clipboard)))
    flags = flags | GDK_DEBUG_CLIPBOARD;

  gdk_display_set_debug_flags (gdk_display_get_default (), flags);
}

static void
gtk_inspector_logs_class_init (GtkInspectorLogsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gtk/libgtk/inspector/logs.ui");
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, text);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, view);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, disable_button);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, events);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, misc);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, dnd);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, input);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, cursor);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, eventloop);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, frames);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, settings);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, opengl);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, vulkan);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, selection);
  gtk_widget_class_bind_template_child_private (widget_class, GtkInspectorLogs, clipboard);
  gtk_widget_class_bind_template_callback (widget_class, disable_toggled);
  gtk_widget_class_bind_template_callback (widget_class, clear_clicked);
  gtk_widget_class_bind_template_callback (widget_class, save_clicked);
  gtk_widget_class_bind_template_callback (widget_class, flag_toggled);
}

// vim: set et sw=2 ts=2:
