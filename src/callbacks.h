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

#define _LIBINTL_H
#include <gtk/gtk.h>
#include <slapt.h>
#define RC_LOCATION "/etc/slapt-get/slapt-getrc"

void on_gslapt_destroy (GtkObject *object, gpointer user_data);
void update_callback (GtkObject *object, gpointer user_data);
void upgrade_callback (GtkObject *object, gpointer user_data);
void execute_callback (GtkObject *object, gpointer user_data);
void open_preferences (GtkMenuItem *menuitem, gpointer user_data);
void search_button_clicked (GtkWidget *gslapt, gpointer user_data);
void add_pkg_for_install (GtkWidget *gslapt, gpointer user_data);
void add_pkg_for_removal (GtkWidget *gslapt, gpointer user_data);
void add_pkg_for_exclude (GtkWidget *gslapt, gpointer user_data);
void build_installed_treeviewlist(GtkWidget *);
void build_available_treeviewlist(GtkWidget *);
void build_searched_treeviewlist(GtkWidget *,gchar *pattern);
void open_about (GtkObject *object, gpointer user_data);

void show_pkg_details (GtkTreeSelection *selection, gpointer data);
void fillin_pkg_details(pkg_info_t *pkg);
void clear_treeview(GtkTreeView *treeview);

void get_package_data(void);
void rebuild_treeviews(void);
guint gslapt_set_status(const gchar *);
void gslapt_clear_status(guint context_id);
void lock_toolbar_buttons(void);
void unlock_toolbar_buttons(void);

void preferences_sources_add(GtkWidget *w, gpointer user_data);
void preferences_sources_remove(GtkWidget *w, gpointer user_data);
void preferences_on_ok_clicked(GtkWidget *w, gpointer user_data);

void on_transaction_okbutton1_clicked(GtkWidget *w, gpointer user_data);
void preferences_exclude_add(GtkWidget *w, gpointer user_data);
void preferences_exclude_remove(GtkWidget *w, gpointer user_data);

void build_sources_treeviewlist(GtkWidget *treeview, const rc_config *global_config);
void build_exclude_treeviewlist(GtkWidget *treeview, const rc_config *global_config);

void populate_transaction_window(GtkWidget *trans_window);


void clear_button_clicked(GtkWidget *button,gpointer user_data);
void build_upgrade_list(void);

gboolean download_packages(void);
int gtk_progress_callback(void *data, double dltotal, double dlnow, double ultotal, double ulnow);
void clean_callback(GtkMenuItem *menuitem, gpointer user_data);
gboolean install_packages(void);
void build_package_treeviewlist(GtkWidget *treeview);
static gboolean write_preferences(void);

void cancel_preferences(GtkWidget *w, gpointer user_data);
void cancel_transaction(GtkWidget *w, gpointer user_data);
void cancel_upgrade_transaction(GtkWidget *w,gpointer user_data);

