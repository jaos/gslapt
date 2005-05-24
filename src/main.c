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

rc_config *global_config; /* our config struct */
struct pkg_list *installed;
struct pkg_list *all;
GtkWidget *gslapt;
transaction_t tran;
transaction_t *trans = &tran;

int main (int argc, char *argv[]) {
  GtkStatusbar *bar;
  guint default_context_id;

#ifdef ENABLE_NLS
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  textdomain (GETTEXT_PACKAGE);
#endif

  /* gtk_set_locale (); */
  g_thread_init(NULL);
  gdk_threads_init();
  gtk_init (&argc, &argv);

  add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");

  gslapt = (GtkWidget *)create_gslapt ();
  gtk_widget_show (gslapt);

  global_config = read_rc_config(RC_LOCATION);
  working_dir_init(global_config);
  chdir(global_config->working_dir);
  global_config->progress_cb = gtk_progress_callback;

  /* read in all pkgs and installed pkgs */
  installed = get_installed_pkgs();
  all = get_available_pkgs();

  build_package_treeviewlist(
     (GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview"));

  bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));
  default_context_id = gtk_statusbar_get_context_id(bar,"default");
  gtk_statusbar_push(bar,default_context_id,_("Ready"));

  gtk_widget_set_sensitive( lookup_widget(gslapt,
                            "pkg_info_action_install_upgrade_button"), FALSE);
  gtk_widget_set_sensitive( lookup_widget(gslapt,
                            "pkg_info_action_remove_button"), FALSE);
  gtk_widget_set_sensitive( lookup_widget(gslapt,
                            "pkg_info_action_exclude_button"), FALSE);
  gtk_widget_set_sensitive( lookup_widget(gslapt,
                            "action_bar_execute_button"), FALSE);

  init_transaction(trans);

  gdk_threads_enter();
  gtk_main ();
  gdk_threads_leave();

  return 0;
}

