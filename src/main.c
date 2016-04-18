/*
 * Copyright (C) 2003-2016 Jason Woodward <woodwardj at jaos dot org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include "callbacks.h"
#include "settings.h"
#include "series.h"

slapt_rc_config *global_config; /* our config struct */
slapt_pkg_list_t *installed;
slapt_pkg_list_t *all;
GtkWidget *gslapt;
GtkBuilder *gslapt_builder;
slapt_transaction_t *trans = NULL;
char rc_location[1024];
GslaptSettings *gslapt_settings = NULL;
GHashTable *gslapt_series_map = NULL;

int main (int argc, char *argv[]) {
  GtkStatusbar *bar;
  guint default_context_id;
  GtkEntryCompletion *completions;
  gchar **pkg_inst_args = slapt_malloc(sizeof **pkg_inst_args);
  guint pkg_inst_args_count = 0;
  gchar **pkg_rem_args = slapt_malloc(sizeof **pkg_rem_args);
  guint pkg_rem_args_count = 0;
  gchar *rc = NULL;
  guint option_index = 0;
  guint do_upgrade = 0;

#ifdef ENABLE_NLS
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  textdomain (GETTEXT_PACKAGE);
#endif

#ifdef SLAPT_HAS_GPGME
  gpgme_check_version (NULL);
#endif

  /* gtk_set_locale (); */
#if !GLIB_CHECK_VERSION (2, 31, 0)
  g_thread_init(NULL);
#endif
  gdk_threads_init();
  gtk_init (&argc, &argv);

  trans = slapt_init_transaction();

  /* series name mapping */
  gslapt_series_map = gslapt_series_map_init();
  gslapt_series_map_fill(gslapt_series_map);

  for (option_index = 1; option_index < argc; ++option_index) {

    if (strcmp(argv[option_index],"--upgrade") == 0) {

      do_upgrade = 1;

    } else if (strcmp(argv[option_index],"--config") == 0) {

      if (argc > (option_index + 1) &&
      strcmp(argv[option_index + 1],"--upgrade") != 0 &&
      strcmp(argv[option_index + 1],"--config") != 0 &&
      strcmp(argv[option_index + 1],"--remove") != 0)
        rc = argv[++option_index];

    } else if (strcmp(argv[option_index],"--install") == 0) {
      char *next_opt = NULL;

      if (argc > (option_index + 1) &&
      strcmp(argv[option_index + 1],"--upgrade") != 0 &&
      strcmp(argv[option_index + 1],"--config") != 0 &&
      strcmp(argv[option_index + 1],"--remove") != 0)
        next_opt = argv[++option_index];

      while (next_opt != NULL) {
        char **tmp = NULL;

        tmp = realloc(pkg_inst_args, sizeof *pkg_inst_args * (pkg_inst_args_count + 1));
        if (tmp == NULL)
          exit(1);
        pkg_inst_args = tmp;
        pkg_inst_args[pkg_inst_args_count] = g_strdup(next_opt);
        ++pkg_inst_args_count;

        if (argc > (option_index + 1) &&
        strcmp(argv[option_index + 1],"--upgrade") != 0 &&
        strcmp(argv[option_index + 1],"--config") != 0 &&
        strcmp(argv[option_index + 1],"--remove") != 0)
          next_opt = argv[++option_index];
        else
          next_opt = NULL;

      }

    } else if (strcmp(argv[option_index],"--remove") == 0) {
      char *next_opt = NULL;

      if (argc > (option_index + 1) &&
      strcmp(argv[option_index + 1],"--upgrade") != 0 &&
      strcmp(argv[option_index + 1],"--config") != 0 &&
      strcmp(argv[option_index + 1],"--install") != 0)
        next_opt = argv[++option_index];

      while (next_opt != NULL) {
        char **tmp = NULL;

        tmp = realloc(pkg_rem_args, sizeof *pkg_rem_args * (pkg_rem_args_count + 1));
        if (tmp == NULL)
          exit(1);
        pkg_rem_args = tmp;
        pkg_rem_args[pkg_rem_args_count] = g_strdup(next_opt);
        ++pkg_rem_args_count;

        if (argc > (option_index + 1) &&
        strcmp(argv[option_index + 1],"--upgrade") != 0 &&
        strcmp(argv[option_index + 1],"--config") != 0 &&
        strcmp(argv[option_index + 1],"--install") != 0)
          next_opt = argv[++option_index];
        else
          next_opt = NULL;

      }
    }

  }

  if (rc == NULL) {
    global_config = slapt_read_rc_config(RC_LOCATION);
    strncpy(rc_location,RC_LOCATION,1023);
  } else {
    global_config = slapt_read_rc_config(rc);
    strncpy(rc_location,rc,1023);
  }
  if (global_config == NULL)
    exit(1);
  slapt_working_dir_init(global_config);
  chdir(global_config->working_dir);
  global_config->progress_cb = gtk_progress_callback;

  /* read in all pkgs and installed pkgs */
  installed = slapt_get_installed_pkgs();
  all = slapt_get_available_pkgs();

  gslapt_builder = gtk_builder_new ();
  gtk_builder_set_translation_domain (gslapt_builder, GETTEXT_PACKAGE);
  gslapt_load_ui (gslapt_builder, "gslapt.ui");

  gslapt = GTK_WIDGET (gtk_builder_get_object (gslapt_builder, "gslapt"));
  gtk_builder_connect_signals (gslapt_builder, NULL);
  // g_object_unref (G_OBJECT (gslapt_builder)); 

  completions = build_search_completions();
  gtk_entry_set_completion(GTK_ENTRY(gtk_builder_get_object (gslapt_builder,"search_entry")),completions);

  build_treeview_columns(
     GTK_WIDGET(gtk_builder_get_object (gslapt_builder,"pkg_listing_treeview")));
  /*
    this sometimes screws up resizing of the window (why?)
  g_thread_create((GThreadFunc)build_package_treeviewlist,
     GTK_WIDGET(gtk_builder_get_object (gslapt_builder,"pkg_listing_treeview")),FALSE,NULL);
  */
  build_package_treeviewlist(GTK_WIDGET(gtk_builder_get_object (gslapt_builder,"pkg_listing_treeview")));

  bar = GTK_STATUSBAR(gtk_builder_get_object (gslapt_builder,"bottom_statusbar"));
  default_context_id = gtk_statusbar_get_context_id(bar,"default");
  gtk_statusbar_push(bar,default_context_id,(gchar *)_("Ready"));

  gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object (gslapt_builder,
                            "action_bar_execute_button")),FALSE);
  gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object (gslapt_builder,"execute1")),FALSE);
  gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object (gslapt_builder,"unmark_all1")),FALSE);

  /* restore previous rc settings */
  gslapt_settings = gslapt_read_rc();
  if (gslapt_settings == NULL) {
    gslapt_settings = gslapt_new_rc();
    gtk_window_set_default_size (GTK_WINDOW (gslapt), 640, 480);
  } else {
    gtk_window_set_default_size(GTK_WINDOW(gslapt),
      gslapt_settings->width, gslapt_settings->height);
    gtk_window_move(GTK_WINDOW(gslapt),
      gslapt_settings->x, gslapt_settings->y);
  }

  gtk_widget_show_all (gslapt);

  if (do_upgrade == 1) {
    g_signal_emit_by_name(gtk_builder_get_object (gslapt_builder,"action_bar_upgrade_button"),"clicked");
  } else {
    if (pkg_inst_args_count > 0){
      int i;
      for (i = 0; i < pkg_inst_args_count; ++i) {
        slapt_pkg_info_t *p = slapt_get_newest_pkg(all,pkg_inst_args[i]);
        slapt_pkg_info_t *inst_p = slapt_get_newest_pkg(installed,pkg_inst_args[i]);

        if (p == NULL)
          continue;

        if ( inst_p != NULL && slapt_cmp_pkgs(inst_p,p) == 0) {
          continue;
        } else if ( inst_p != NULL && slapt_cmp_pkgs(inst_p,p) < 0) {
          if (slapt_add_deps_to_trans(global_config,trans,all,installed,p) == 0) {
            slapt_add_upgrade_to_transaction(trans,inst_p,p);
          } else {
            exit(1);
          }
        } else {
          if (slapt_add_deps_to_trans(global_config,trans,all,installed,p) == 0) {
            slapt_pkg_list_t *conflicts = slapt_is_conflicted(trans,all,installed,p);
            slapt_add_install_to_transaction(trans,p);
            if ( conflicts->pkg_count > 0) {
              unsigned int cindex = 0;
              for (cindex = 0; cindex < conflicts->pkg_count; cindex++) {
                slapt_add_remove_to_transaction(trans,conflicts->pkgs[cindex]);
              }
            }
            slapt_free_pkg_list(conflicts);
          } else {
            exit(1);
          }
        }
      }
    }
    if (pkg_rem_args_count > 0) {
      int i;
      for (i = 0; i < pkg_rem_args_count; ++i) {
        slapt_pkg_info_t *r = slapt_get_newest_pkg(installed,pkg_rem_args[i]);
        if (r != NULL) {
          slapt_add_remove_to_transaction(trans,r);
        }
      }
    }
  }

  for (option_index = 0; option_index < pkg_inst_args_count; ++option_index) {
    g_free(pkg_inst_args[option_index]);
  }
  g_free(pkg_inst_args);
  for (option_index = 0; option_index < pkg_rem_args_count; ++option_index) {
    g_free(pkg_rem_args[option_index]);
  }
  g_free(pkg_rem_args);

  if ( trans->remove_pkgs->pkg_count > 0 || trans->install_pkgs->pkg_count > 0 || trans->upgrade_pkgs->pkg_count > 0) {
    g_signal_emit_by_name(gtk_builder_get_object (gslapt_builder,"action_bar_execute_button"),"clicked");
  }

  gdk_threads_enter();
  gtk_main ();
  gdk_threads_leave();

  return 0;
}

