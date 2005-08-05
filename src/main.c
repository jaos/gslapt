/*
 * Copyright (C) 2003,2004,2005 Jason Woodward <woodwardj at jaos dot org>
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
#include "interface.h"
#include "support.h"

slapt_rc_config *global_config; /* our config struct */
struct slapt_pkg_list *installed;
struct slapt_pkg_list *all;
GtkWidget *gslapt;
slapt_transaction_t tran;
slapt_transaction_t *trans = &tran;

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

  /* gtk_set_locale (); */
  g_thread_init(NULL);
  gdk_threads_init();
  gtk_init (&argc, &argv);

  slapt_init_transaction(trans);

  add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");

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
  } else {
    global_config = slapt_read_rc_config(rc);
    g_free(rc);
  }
  slapt_working_dir_init(global_config);
  chdir(global_config->working_dir);
  global_config->progress_cb = gtk_progress_callback;

  /* read in all pkgs and installed pkgs */
  installed = slapt_get_installed_pkgs();
  all = slapt_get_available_pkgs();

  gslapt = (GtkWidget *)create_gslapt();

  completions = build_search_completions();
  gtk_entry_set_completion(GTK_ENTRY(lookup_widget(gslapt,"search_entry")),completions);

  build_treeview_columns(
     (GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview"));
  /*
    this sometimes screws up resizing of the window (why?)
  g_thread_create((GThreadFunc)build_package_treeviewlist,
     (GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview"),FALSE,NULL);
  */
  build_package_treeviewlist((GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview"));

  bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));
  default_context_id = gtk_statusbar_get_context_id(bar,"default");
  gtk_statusbar_push(bar,default_context_id,(gchar *)_("Ready"));

  gtk_widget_set_sensitive(lookup_widget(gslapt,
                            "action_bar_execute_button"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"clear_button"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"execute1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"unmark_all1"),FALSE);

  gtk_widget_show (gslapt);

  if (do_upgrade == 1) {
    g_signal_emit_by_name(lookup_widget(gslapt,"action_bar_upgrade_button"),"clicked");
  } else {
    if (pkg_inst_args_count > 0){
      int i;
      for (i = 0; i < pkg_inst_args_count; ++i) {
        slapt_pkg_info_t *p = slapt_get_newest_pkg(all,pkg_inst_args[i]);
        slapt_pkg_info_t *inst_p = slapt_get_newest_pkg(installed,pkg_inst_args[i]);

        if (p == NULL)
          continue;

        if ( inst_p != NULL && slapt_cmp_pkgs(inst_p,p) < 0) {
          if (slapt_add_deps_to_trans(global_config,trans,all,installed,p) == 0) {
            slapt_add_upgrade_to_transaction(trans,inst_p,p);
          } else {
            exit(1);
          }
        } else {
          if (slapt_add_deps_to_trans(global_config,trans,all,installed,p) == 0) {
            slapt_pkg_info_t *conflict_p;
            slapt_add_install_to_transaction(trans,p);
            if ( (conflict_p = slapt_is_conflicted(trans,all,installed,p)) != NULL) {
              slapt_add_remove_to_transaction(trans,conflict_p);
            }
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
    g_signal_emit_by_name(lookup_widget(gslapt,"action_bar_execute_button"),"clicked");
  }

  gdk_threads_enter();
  gtk_main ();
  gdk_threads_leave();

  return 0;
}

