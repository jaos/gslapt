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
# include <config.h>
#endif

#define _GNU_SOURCE

#include <gtk/gtk.h>

#include "callbacks.h"
#include "interface.h"
#include "support.h"

static GtkWidget *progress_window;
static guint pending_trans_context_id = 0;
static int disk_space(const rc_config *global_config,int space_needed );
static void pkg_action_popup_menu(GtkTreeView *treeview, gpointer data);

void on_gslapt_destroy (GtkObject *object, gpointer user_data) 
{
  extern transaction_t *trans;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  extern rc_config *global_config;

  free_transaction(trans);
  free_pkg_list(all);
  free_pkg_list(installed);
  free_rc_config(global_config);

  gtk_main_quit();
  exit(1);
}

void update_callback (GtkObject *object, gpointer user_data) 
{
  GThread *gpd;

  clear_execute_active();

  gpd = g_thread_create((GThreadFunc)get_package_data,NULL,FALSE,NULL);

  return;
}

void upgrade_callback (GtkObject *object, gpointer user_data) 
{
  GtkWidget *w;
  extern transaction_t *trans;

  free_transaction(trans);
  init_transaction(trans);
  clear_execute_active();

  build_upgrade_list();

  if ( trans->install_pkgs->pkg_count == 0 &&
    trans->remove_pkgs->pkg_count == 0  &&
    trans->upgrade_pkgs->pkg_count == 0 &&
    trans->missing_err->err_count == 0 &&
    trans->conflict_err->err_count == 0
  ) {
    notify(_("Up to Date"),
           _("<span weight=\"bold\" size=\"large\">No updates available</span>"));
  }else{
    GtkWidget *button;
    w = (GtkWidget *)create_transaction_window();
    button = lookup_widget(w,"button2");
    g_signal_handlers_disconnect_by_func((gpointer)button,cancel_transaction,GTK_OBJECT(w));
    g_signal_connect_swapped((gpointer) button,"clicked",G_CALLBACK(cancel_upgrade_transaction),GTK_OBJECT(w));
    if ( populate_transaction_window(w) == 0 ) {
      gtk_widget_show(w);
    } else {
      gtk_widget_destroy(w);
    }
  }
}

void execute_callback (GtkObject *object, gpointer user_data) 
{
  GtkWidget *trans_window;
  extern transaction_t *trans;

  if (
    trans->install_pkgs->pkg_count == 0
    && trans->upgrade_pkgs->pkg_count == 0
    && trans->remove_pkgs->pkg_count == 0
  ) return;

  trans_window = (GtkWidget *)create_transaction_window();
  if ( populate_transaction_window(trans_window) == 0 ) {
    gtk_widget_show(trans_window);
  } else {
    gtk_widget_destroy(trans_window);
  }
}

void open_preferences (GtkMenuItem *menuitem, gpointer user_data) 
{
  GtkWidget *preferences;
  extern rc_config *global_config;
  GtkEntry *working_dir;
  GtkTreeView *source_tree,*exclude_tree;

  preferences = (GtkWidget *)create_window_preferences();

  working_dir = GTK_ENTRY(lookup_widget(preferences,"preferences_working_dir_entry"));
  gtk_entry_set_text(working_dir,global_config->working_dir);

  source_tree = GTK_TREE_VIEW(lookup_widget(preferences,"preferences_sources_treeview"));
  build_sources_treeviewlist((GtkWidget *)source_tree,global_config);
  exclude_tree = GTK_TREE_VIEW(lookup_widget(preferences,"preferences_exclude_treeview"));
  build_exclude_treeviewlist((GtkWidget *)exclude_tree,global_config);

  gtk_widget_show(preferences);
}

void search_button_clicked (GtkWidget *gslapt, gpointer user_data) 
{
  GtkTreeView *treeview;
  gchar *pattern;

  /* search_entry */
  pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  clear_treeview(treeview);
  build_searched_treeviewlist(GTK_WIDGET(treeview),pattern);
}

void add_pkg_for_install (GtkWidget *gslapt, gpointer user_data) 
{
  extern transaction_t *trans;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  extern rc_config *global_config;
  pkg_info_t *pkg = NULL, *installed_pkg = NULL;
  GtkTreeView *treeview;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkWidget *caller_button = (GtkWidget *)user_data;
  GtkLabel *caller_button_label = gtk_bin_get_child(caller_button);

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter) == TRUE) {
    const gchar *pkg_name;
    const gchar *pkg_version;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);

    if ( pkg_name == NULL || pkg_version == NULL ) {
      fprintf(stderr,"failed to get package name and version from selection\n");
      return;
    }

    if ( strcmp(gtk_label_get_text(caller_button_label),_("Upgrade")) == 0) {
      pkg = get_newest_pkg(all,pkg_name);
    } else {
      pkg = get_exact_pkg(all,pkg_name,pkg_version);
    }

    if ( pkg == NULL ) {
      fprintf(stderr,"Failed to find package: %s-%s\n",pkg_name,pkg_version);
      g_free(pkg_name);
      g_free(pkg_version);
      return;
    }

    installed_pkg = get_newest_pkg(installed,pkg_name);

    g_free(pkg_name);
    g_free(pkg_version);

  }

  if ( pkg == NULL ) {
    fprintf(stderr,"No package to work with\n");
    return;
  }

  /* if it is not already installed, install it */
  if ( installed_pkg == NULL ) {

    if ( add_deps_to_trans(global_config,trans,all,installed,pkg) == 0 ) {
      pkg_info_t *conflicted_pkg = NULL;
      gchar *status = NULL;

      /* if there is a conflict, we schedule the conflict for removal */
      if ( (conflicted_pkg = is_conflicted(trans,all,installed,pkg)) != NULL ) {
        add_remove_to_transaction(trans,conflicted_pkg);
        set_execute_active();
      }

      add_install_to_transaction(trans,pkg);
      gtk_list_store_set(model,&iter,0,create_pixbuf("pkg_action_install.png"),-1);
      status = g_strdup_printf("i%s",pkg->name);
      gtk_list_store_set(model,&iter,4,status,-1);
      set_execute_active();
      g_free(status);

    }else{
      fprintf(stderr,"Excluding %s due to dependency failure\n",pkg->name);
      add_exclude_to_transaction(trans,pkg);
    }

  }else{ /* else we upgrade or reinstall */

    /* it is already installed, attempt an upgrade */
    if (
      ((cmp_pkg_versions(installed_pkg->version,pkg->version)) < 0) ||
      (global_config->re_install == TRUE)
    ) {

      if ( add_deps_to_trans(global_config,trans,all,installed,pkg) == 0 ) {
        pkg_info_t *conflicted_pkg = NULL;

        if ( (conflicted_pkg = is_conflicted(trans,all,installed,pkg)) != NULL ) {
          fprintf(stderr,"%s conflicts with %s\n",pkg->name,conflicted_pkg->name);
          add_remove_to_transaction(trans,conflicted_pkg);
          set_execute_active();
        }else{
          gchar *status = g_strdup_printf("u%s",pkg->name);
          add_upgrade_to_transaction(trans,installed_pkg,pkg);
          gtk_list_store_set(model,&iter,0,create_pixbuf("pkg_action_install.png"),-1);
          gtk_list_store_set(model,&iter,4,status,-1);
          set_execute_active();
          g_free(status);
        }

      }else{
        fprintf(stderr,"Excluding %s due to dependency failure\n",pkg->name);
        add_exclude_to_transaction(trans,pkg);
      }

    }

  }

}

void add_pkg_for_removal (GtkWidget *gslapt, gpointer user_data) 
{
  extern transaction_t *trans;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  extern rc_config *global_config;
  GtkTreeView *treeview;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeSelection *selection;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter) == TRUE) {
    const gchar *pkg_name;
    const gchar *pkg_version;
    pkg_info_t *pkg;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);

    if ( (pkg = get_exact_pkg(installed,pkg_name,pkg_version)) != NULL ) {
      guint c;
      struct pkg_list *deps;
      gchar *status = NULL;

      deps = is_required_by(global_config,all,pkg);

      for (c = 0; c < deps->pkg_count;c++) {
        if ( get_exact_pkg(installed,deps->pkgs[c]->name,deps->pkgs[c]->version) != NULL ) {
          add_remove_to_transaction(trans,deps->pkgs[c]);
        }
          set_execute_active();
      }

      free(deps->pkgs);
      free(deps);

      add_remove_to_transaction(trans,pkg);
      gtk_list_store_set(model,&iter,0,create_pixbuf("pkg_action_remove.png"),-1);
      status = g_strdup_printf("r%s",pkg->name);
      gtk_list_store_set(model,&iter,4,status,-1);
      g_free(status);
      set_execute_active();

    }

    g_free(pkg_name);
    g_free(pkg_version);

  }


}

void build_package_treeviewlist(GtkWidget *treeview)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  GtkWidget *image;
  guint i = 0;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;

  store = gtk_list_store_new (
    5, /* name, version, location, installed */
    GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
  );

  for (i = 0; i < all->pkg_count; i++ ) {
    /* we use this for sorting the status */
    /* a=installed,i=install,r=remove,u=upgrade,z=available */
    gchar *status = NULL;
    guint is_inst = 0;
    GdkPixbuf *status_icon = NULL;

    if (get_exact_pkg(installed,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      is_inst = 1;
    }

    if (get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_available.png");
      status = g_strdup_printf("z%s",all->pkgs[i]->name);
    } else if (get_exact_pkg(trans->remove_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_remove.png");
      status = g_strdup_printf("r%s",all->pkgs[i]->name);
    } else if (get_exact_pkg(trans->install_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_install.png");
      status = g_strdup_printf("i%s",all->pkgs[i]->name);
    } else if (search_transaction_by_pkg(trans,all->pkgs[i]) == 1) {
      status_icon = create_pixbuf("pkg_action_install.png");
      status = g_strdup_printf("u%s",all->pkgs[i]->name);
    } else if (is_inst == 1) {
      status_icon = create_pixbuf("pkg_action_installed.png");
      status = g_strdup_printf("a%s",all->pkgs[i]->name);
    } else {
      status_icon = create_pixbuf("pkg_action_available.png");
      status = g_strdup_printf("z%s",all->pkgs[i]->name);
    }

    gtk_list_store_append (store, &iter);
    gtk_list_store_set ( store, &iter,
      0,status_icon,
      1,all->pkgs[i]->name,
      2,all->pkgs[i]->version,
      3,all->pkgs[i]->location,
      4,status,
      -1
    );

    g_free(status);
  }
  for (i = 0; i < installed->pkg_count; ++i) {
    if ( get_exact_pkg(all,installed->pkgs[i]->name,installed->pkgs[i]->version) == NULL ) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      GdkPixbuf *status_icon = NULL;

      if (get_exact_pkg(trans->remove_pkgs,installed->pkgs[i]->name,installed->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",installed->pkgs[i]->name);
      } else {
        status_icon = create_pixbuf("pkg_action_installed.png");
        status = g_strdup_printf("a%s",installed->pkgs[i]->name);
      }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set ( store, &iter,
        0,status_icon,
        1,installed->pkgs[i]->name,
        2,installed->pkgs[i]->version,
        3,installed->pkgs[i]->location,
        4,status
        -1
      );

      g_free(status);
    }
  }

  /* column for installed status */
  renderer = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes (_("Status"), renderer,
    "pixbuf", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 4);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for name */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
    "text", 1, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 1);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for version */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Version"), renderer,
    "text", 2, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 2);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for location */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Location"), renderer,
    "text", 3, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 3);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* invisible column to sort installed by */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Installed"), renderer,
    "text", 4, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
    G_CALLBACK (show_pkg_details), NULL);

  g_signal_connect(G_OBJECT(treeview),"cursor-changed",
    G_CALLBACK(pkg_action_popup_menu),NULL);

}


void build_searched_treeviewlist(GtkWidget *treeview, gchar *pattern)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  GdkPixbuf *image;
  guint i = 0;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;

  store = gtk_list_store_new (
    5, /* name, version, location, installed */
    GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
  );

  if ( pattern != NULL ) {
    struct pkg_list *a_matches,*i_matches;

    a_matches = search_pkg_list(all,pattern);
    for (i = 0; i < a_matches->pkg_count; i++ ) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      guint is_inst = 0;
      GdkPixbuf *status_icon = NULL;

      if (get_exact_pkg(installed,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        is_inst = 1;
      }

      if (get_exact_pkg(trans->exclude_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_available.png");
        status = g_strdup_printf("z%s",a_matches->pkgs[i]->name);
      } else if (get_exact_pkg(trans->remove_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",a_matches->pkgs[i]->name);
      } else if (get_exact_pkg(trans->install_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_install.png");
        status = g_strdup_printf("i%s",a_matches->pkgs[i]->name);
      } else if (search_transaction_by_pkg(trans,a_matches->pkgs[i]) == 1) {
        status_icon = create_pixbuf("pkg_action_install.png");
        status = g_strdup_printf("u%s",a_matches->pkgs[i]->name);
      } else if (is_inst == 1) {
        status_icon = create_pixbuf("pkg_action_installed.png");
        status = g_strdup_printf("a%s",a_matches->pkgs[i]->name);
      } else {
        status_icon = create_pixbuf("pkg_action_available.png");
        status = g_strdup_printf("z%s",a_matches->pkgs[i]->name);
      }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set ( store, &iter,
        0,status_icon,
        1,a_matches->pkgs[i]->name,
        2,a_matches->pkgs[i]->version,
        3,a_matches->pkgs[i]->location,
        4,status,
        -1
      );
      g_free(status);
    }

    i_matches = search_pkg_list(installed,pattern);
    for (i = 0; i < i_matches->pkg_count; i++ ) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      GdkPixbuf *status_icon = NULL;

      if ( get_exact_pkg( a_matches, i_matches->pkgs[i]->name,
                          i_matches->pkgs[i]->version) != NULL ) {
         continue;
      }

      if (get_exact_pkg(trans->remove_pkgs,i_matches->pkgs[i]->name,i_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",i_matches->pkgs[i]->name);
      } else {
        status_icon = create_pixbuf("pkg_action_installed.png");
        status = g_strdup_printf("a%s",i_matches->pkgs[i]->name);
      }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set ( store, &iter,
        0,status_icon,
        1,i_matches->pkgs[i]->name,
        2,i_matches->pkgs[i]->version,
        3,i_matches->pkgs[i]->location,
        4,status,
        -1
      );

      g_free(status);
    }
    free_pkg_list(a_matches);
    free_pkg_list(i_matches);

  }

  /* column for installed status */
  renderer = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes (_("Status"), renderer,
    "pixbuf", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 4);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for name */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Name"), renderer,
    "text", 1, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 1);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for version */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Version"), renderer,
    "text", 2, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 2);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for location */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Location"), renderer,
    "text", 3, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 3);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* invisible column to sort installed by */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ("", renderer,
    "text", 4, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
    G_CALLBACK (show_pkg_details), NULL);
  g_signal_connect(G_OBJECT(treeview),"cursor-changed",
    G_CALLBACK(pkg_action_popup_menu),NULL);

}


void open_about (GtkObject *object, gpointer user_data) 
{
  GtkWidget *about;
  about = (GtkWidget *)create_about();
  gtk_label_set_text(GTK_LABEL(lookup_widget(about,"label146")),
    "<span weight=\"bold\" size=\"xx-large\">" PACKAGE " " VERSION "</span>");
  gtk_label_set_use_markup(GTK_LABEL(lookup_widget(about,"label146")),TRUE);
  gtk_widget_show (about);
}

void show_pkg_details (GtkTreeSelection *selection, gpointer data) 
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;

  if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
    gchar *p_name,*p_version,*p_location;
    pkg_info_t *pkg;

    gtk_tree_model_get (model, &iter, 1, &p_name, -1);
    gtk_tree_model_get (model, &iter, 2, &p_version, -1);
    gtk_tree_model_get (model, &iter, 3, &p_location, -1);

    pkg = get_pkg_by_details(all,p_name,p_version,p_location);
    if ( pkg != NULL ) {
      fillin_pkg_details(pkg);
    }else{
      pkg = get_pkg_by_details(installed,p_name,p_version,p_location);
      if ( pkg != NULL )
        fillin_pkg_details(pkg);
    }

    g_free (p_name);
    g_free (p_version);
    g_free (p_location);
  }

}

static void fillin_pkg_details(pkg_info_t *pkg)
{
  extern GtkWidget *gslapt;
  gchar size_c[20],size_u[20],*short_desc;
  GtkTextBuffer *pkg_full_desc;

  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_name")),pkg->name);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_location")),pkg->location);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_version")),pkg->version);
  if ( pkg->mirror != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_mirror")),pkg->mirror);
  }
  sprintf(size_c,"%d K",pkg->size_c);
  sprintf(size_u,"%d K",pkg->size_u);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_size")),size_c);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_installed_size")),size_u);

  if ( pkg->required != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_required")),pkg->required);
  }
  if ( pkg->conflicts != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_conflicts")),pkg->conflicts);
  }
  if ( pkg->suggests != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_suggests")),pkg->suggests);
  }

  short_desc = gen_short_pkg_description(pkg);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_description")),short_desc);
  free(short_desc);

  pkg_full_desc = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(gslapt,"pkg_description_textview")));
  gtk_text_buffer_set_text(pkg_full_desc,pkg->description,-1);

}

static void clear_treeview(GtkTreeView *treeview)
{
  GtkListStore *store;
  GList *columns;
  guint i;

  store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));
  gtk_list_store_clear(store);
  columns = gtk_tree_view_get_columns(treeview);
  for (i = 0; i < g_list_length(columns); i++ ) {
    GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
    if ( column != NULL ) {
      gtk_tree_view_remove_column(treeview,column);
    }
  }
  g_list_free(columns);
  g_signal_handlers_disconnect_by_func((gpointer)treeview,pkg_action_popup_menu,NULL);
}

static void get_package_data(void)
{
  GtkLabel *progress_action_label,*progress_message_label,*progress_pkg_desc;
  GtkProgressBar *p_bar;
  extern rc_config *global_config;
  extern transaction_t *trans;
  guint i,context_id;
  FILE *pkg_list_fh_tmp = NULL;
  gfloat dl_files = 0.0, dl_count = 0.0;
  ssize_t bytes_read;
  size_t getline_len = 0;
  gchar *getline_buffer = NULL;
  FILE *pkg_list_fh;


  progress_window = create_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),_("Progress"));
  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  lock_toolbar_buttons();
  context_id = gslapt_set_status(_("Checking for new package data..."));
  gtk_widget_show(progress_window);
  gdk_threads_leave();

  /* open tmp pkg list file */
  pkg_list_fh_tmp = tmpfile();
  if ( pkg_list_fh_tmp == NULL ) {

    if ( errno ) perror("tmpfile");

    exit(1);
  }

  dl_files = (global_config->sources->count * 3.0 );

  /* go through each package source and download the meta data */
  for (i = 0; i < global_config->sources->count; i++) {
    FILE *tmp_pkg_f,*tmp_patch_f,*tmp_checksum_f;
    struct pkg_list *available_pkgs = NULL;
    struct pkg_list *patch_pkgs = NULL;
    gchar *pkg_filename,*patch_filename,*checksum_filename;
    gchar *pkg_head,*pkg_local_head;
    gchar *patch_head,*patch_local_head;
    gchar *checksum_head,*checksum_local_head;
    guint a;

    gdk_threads_enter();
    gtk_label_set_text(progress_action_label,global_config->sources->url[i]);
    gtk_label_set_text(progress_message_label,PKG_LIST);
    gdk_threads_leave();

    /* download our PKG_LIST */
    pkg_filename = gen_filename_from_url(global_config->sources->url[i],PKG_LIST);
    pkg_head = head_mirror_data(global_config->sources->url[i],PKG_LIST);
    pkg_local_head = read_head_cache(pkg_filename);

    /* open for reading if cached, otherwise write it from the downloaded data */
    if ( pkg_head != NULL && pkg_local_head != NULL && strcmp(pkg_head,pkg_local_head) == 0) {

      if ( (tmp_pkg_f = open_file(pkg_filename,"r")) == NULL ) exit(1);

      available_pkgs = parse_packages_txt(tmp_pkg_f);
    }else{

      if ( (tmp_pkg_f = open_file(pkg_filename,"w+b")) == NULL ) exit(1);

      if ( get_mirror_data_from_source(tmp_pkg_f,global_config,global_config->sources->url[i],PKG_LIST) == 0 ) {
        rewind(tmp_pkg_f); /* make sure we are back at the front of the file */
        available_pkgs = parse_packages_txt(tmp_pkg_f);
      }else{
        clear_head_cache(pkg_filename);
        free(pkg_head);
        free(pkg_local_head);
        free(pkg_filename);
        fclose(tmp_pkg_f);
        fclose(pkg_list_fh_tmp);
        gdk_threads_enter();
        gtk_widget_destroy(progress_window);
        notify(_("Source download failed"),global_config->sources->url[i]);
        gslapt_clear_status(context_id);
        unlock_toolbar_buttons();
        gdk_threads_leave();
        return;
      }
    }
    if ( available_pkgs == NULL || available_pkgs->pkg_count < 1 ) {
      clear_head_cache(pkg_filename);
      free(pkg_head);
      free(pkg_local_head);
      free(pkg_filename);
      fclose(tmp_pkg_f);
      fclose(pkg_list_fh_tmp);
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      notify(_("Source download failed"),global_config->sources->url[i]);
      gslapt_clear_status(context_id);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }
    /* if all is good, write it */
    if ( pkg_head != NULL ) {
      write_head_cache(pkg_head,pkg_filename);
    }
    free(pkg_head);
    free(pkg_local_head);
    free(pkg_filename);
    fclose(tmp_pkg_f);
    ++dl_count;

    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_label_set_text(progress_message_label,PATCHES_LIST);
    gdk_threads_leave();


    /* download PATCHES_LIST */
    patch_filename = gen_filename_from_url(global_config->sources->url[i],PATCHES_LIST);
    patch_head = head_mirror_data(global_config->sources->url[i],PATCHES_LIST);
    patch_local_head = read_head_cache(patch_filename);

    /* open for reading if cached, otherwise write it from the downloaded data */
    if ( patch_head != NULL && patch_local_head != NULL && strcmp(patch_head,patch_local_head) == 0) {

      if ( (tmp_patch_f = open_file(patch_filename,"r")) == NULL ) exit(1);

      patch_pkgs = parse_packages_txt(tmp_patch_f);
    }else{

      if ( (tmp_patch_f = open_file(patch_filename,"w+b")) == NULL ) exit (1);

      if ( get_mirror_data_from_source(tmp_patch_f,global_config,global_config->sources->url[i],PATCHES_LIST) == 0 ) {
        rewind(tmp_patch_f); /* make sure we are back at the front of the file */
        patch_pkgs = parse_packages_txt(tmp_patch_f);
      }else{
        /* we don't care if the patch fails, for example current doesn't have patches */
        clear_head_cache(patch_filename);
      }
    }
    /* if all is good, write it */
    if ( patch_head != NULL ) write_head_cache(patch_head,patch_filename);
    free(patch_head);
    free(patch_local_head);
    free(patch_filename);
    fclose(tmp_patch_f);
    ++dl_count;

    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_label_set_text(progress_message_label,CHECKSUM_FILE);
    gdk_threads_leave();


    /* download checksum file */
    checksum_filename = gen_filename_from_url(global_config->sources->url[i],CHECKSUM_FILE);
    checksum_head = head_mirror_data(global_config->sources->url[i],CHECKSUM_FILE);
    checksum_local_head = read_head_cache(checksum_filename);

    /* open for reading if cached, otherwise write it from the downloaded data */
    if ( checksum_head != NULL && checksum_local_head != NULL && strcmp(checksum_head,checksum_local_head) == 0) {
      if ( (tmp_checksum_f = open_file(checksum_filename,"r")) == NULL ) exit(1);
    }else{

      if ( (tmp_checksum_f = open_file(checksum_filename,"w+b")) == NULL ) exit(1);

      if ( get_mirror_data_from_source(
            tmp_checksum_f,global_config,global_config->sources->url[i],CHECKSUM_FILE
          ) != 0
      ) {
        clear_head_cache(checksum_filename);
        free(checksum_head);
        free(checksum_local_head);
        free(checksum_filename);
        fclose(tmp_checksum_f);
        fclose(pkg_list_fh_tmp);
        gdk_threads_enter();
        gtk_widget_destroy(progress_window);
        notify(_("Source download failed"),global_config->sources->url[i]);
        gslapt_clear_status(context_id);
        unlock_toolbar_buttons();
        gdk_threads_leave();
        return;
      }
      rewind(tmp_checksum_f); /* make sure we are back at the front of the file */
    }
    /* if all is good, write it */

    if ( checksum_head != NULL ) {
      write_head_cache(checksum_head,checksum_filename);
    }

    free(checksum_head);
    free(checksum_local_head);
    ++dl_count;
    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gdk_threads_leave();

    /*
      only do this double check if we know it didn't fail
    */
    if ( available_pkgs->pkg_count == 0 ) {
      free(checksum_filename);
      fclose(tmp_checksum_f);
      fclose(pkg_list_fh_tmp);
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      notify(_("Source download failed"),global_config->sources->url[i]);
      gslapt_clear_status(context_id);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

    /* now map md5 checksums to packages */
    for (a = 0;a < available_pkgs->pkg_count;a++) {
      get_md5sum(available_pkgs->pkgs[a],tmp_checksum_f);
    }
    for (a = 0;a < patch_pkgs->pkg_count;a++) {
      get_md5sum(patch_pkgs->pkgs[a],tmp_checksum_f);
    }

    /* write package listings to disk */
    write_pkg_data(global_config->sources->url[i],pkg_list_fh_tmp,available_pkgs);
    write_pkg_data(global_config->sources->url[i],pkg_list_fh_tmp,patch_pkgs);

    if ( available_pkgs ) free_pkg_list(available_pkgs);

    if ( patch_pkgs ) free_pkg_list(patch_pkgs);

    free(checksum_filename);
    fclose(tmp_checksum_f);

  }/* end for loop */

  /* if all our downloads where a success, write to PKG_LIST_L */
  if ( (pkg_list_fh = open_file(PKG_LIST_L,"w+")) == NULL ) exit(1);

  if ( pkg_list_fh == NULL ) exit(1);

  rewind(pkg_list_fh_tmp);
  while ( (bytes_read = getline(&getline_buffer,&getline_len,pkg_list_fh_tmp) ) != EOF ) {
    fprintf(pkg_list_fh,"%s",getline_buffer);
  }

  if ( getline_buffer ) free(getline_buffer);

  fclose(pkg_list_fh);

  /* reset our currently selected packages */
  free_transaction(trans);
  init_transaction(trans);

  /* close the tmp pkg list file */
  fclose(pkg_list_fh_tmp);

  gdk_threads_enter();
  unlock_toolbar_buttons();
  rebuild_treeviews();
  gslapt_clear_status(context_id);
  gtk_widget_destroy(progress_window);
  gdk_threads_leave();

}

int gtk_progress_callback(void *data, double dltotal, double dlnow,
                          double ultotal, double ulnow)
{
  extern GtkWidget *gslapt;
  GtkProgressBar *p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress"));
  double perc = 1.0;

  if ( dltotal != 0.0 ) perc = ((dlnow * 100)/dltotal)/100;

  gdk_threads_enter();
  gtk_progress_bar_set_fraction(p_bar,perc);
  gdk_threads_leave();

  return 0;
}

static void rebuild_treeviews(void)
{
  extern GtkWidget *gslapt;
  GtkWidget *treeview;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  struct pkg_list *all_ptr,*installed_ptr;

  installed_ptr = installed;
  all_ptr = all;

  installed = get_installed_pkgs();
  all = get_available_pkgs();

  treeview = (GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview");
  clear_treeview( GTK_TREE_VIEW(treeview) );
  build_package_treeviewlist(treeview);

  free_pkg_list(installed_ptr);
  free_pkg_list(all_ptr);

}

static guint gslapt_set_status(const gchar *msg)
{
  extern GtkWidget *gslapt;
  guint context_id;
  GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));
  context_id = gtk_statusbar_get_context_id(bar,msg);

  gtk_statusbar_push(bar,context_id,msg);

  return context_id;
}

static void gslapt_clear_status(guint context_id)
{
  extern GtkWidget *gslapt;
  GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));

  gtk_statusbar_pop(bar,context_id);
}

static void lock_toolbar_buttons(void)
{
  extern GtkWidget *gslapt;
  GtkToolButton *action_bar_update_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_update_button") );
  GtkToolButton *action_bar_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_upgrade_button") );
  GtkToolButton *action_bar_clean_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_clean_button") );
  GtkToolButton *action_bar_execute_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_execute_button") );

  gtk_widget_set_sensitive((GtkWidget *)action_bar_update_button,FALSE);
  gtk_widget_set_sensitive((GtkWidget *)action_bar_upgrade_button,FALSE);
  gtk_widget_set_sensitive((GtkWidget *)action_bar_clean_button,FALSE);
  gtk_widget_set_sensitive((GtkWidget *)action_bar_execute_button,FALSE);
}

static void unlock_toolbar_buttons(void)
{
  extern GtkWidget *gslapt;
  extern transaction_t *trans;

  GtkToolButton *action_bar_update_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_update_button") );
  GtkToolButton *action_bar_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_upgrade_button") );
  GtkToolButton *action_bar_clean_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_clean_button") );
  GtkToolButton *action_bar_execute_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_execute_button") );

  gtk_widget_set_sensitive((GtkWidget *)action_bar_update_button,TRUE);
  gtk_widget_set_sensitive((GtkWidget *)action_bar_upgrade_button,TRUE);
  gtk_widget_set_sensitive((GtkWidget *)action_bar_clean_button,TRUE);

  if (
    trans->upgrade_pkgs->pkg_count != 0
    || trans->remove_pkgs->pkg_count != 0
    || trans->install_pkgs->pkg_count != 0
  ) {
    set_execute_active();
  }

}

static void lhandle_transaction(GtkWidget *w)
{
  GtkCheckButton *dl_only_checkbutton;
  gboolean dl_only = FALSE;
  extern transaction_t *trans;

  gdk_threads_enter();
  lock_toolbar_buttons();
  gdk_threads_leave();

  dl_only_checkbutton = GTK_CHECK_BUTTON(lookup_widget(w,"download_only_checkbutton"));
  dl_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dl_only_checkbutton));
  gtk_widget_destroy(w);

  /* download the pkgs */
  if ( trans->install_pkgs->pkg_count > 0 || trans->upgrade_pkgs->pkg_count > 0 ) {
    if ( download_packages() == FALSE ) {
      gdk_threads_enter();
      notify(_("Error"),_("Package(s) failed to download"));
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }
  }

  /* return early if download_only is set */
  if ( dl_only == TRUE ) {
    free_transaction(trans);
    init_transaction(trans);
    gdk_threads_enter();
    unlock_toolbar_buttons();
    clear_execute_active();
    gdk_threads_leave();
    return;
  }

  /* begin removing, installing, and upgrading */
  if ( install_packages() == FALSE ) {
    gdk_threads_enter();
    notify(_("Error"),_("pkgtools returned an error"));
    unlock_toolbar_buttons();
    gdk_threads_leave();
    return;
  }

  free_transaction(trans);
  init_transaction(trans);

  gdk_threads_enter();
  unlock_toolbar_buttons();
  clear_execute_active();
  rebuild_treeviews();
  gdk_threads_leave();

}

void on_transaction_okbutton1_clicked(GtkWidget *w, gpointer user_data)
{
  GThread *gdp;

  gdp = g_thread_create((GThreadFunc)lhandle_transaction,w,FALSE,NULL);

  return;

}

static void build_sources_treeviewlist(GtkWidget *treeview,
                                       const rc_config *global_config)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  guint i = 0;

  store = gtk_list_store_new (
    1, /* source url */
    G_TYPE_STRING
  );

  for (i = 0; i < global_config->sources->count; ++i ) {

    if ( global_config->sources->url[i] == NULL ) continue;

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store,&iter,0,global_config->sources->url[i],-1);
  }

  /* column for url */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Source"), renderer,
    "text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

}

static void build_exclude_treeviewlist(GtkWidget *treeview,
                                       const rc_config *global_config)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  guint i = 0;

  store = gtk_list_store_new (
    1, /* exclude expression */
    G_TYPE_STRING
  );

  for (i = 0; i < global_config->exclude_list->count; i++ ) {

    if ( global_config->exclude_list->excludes[i] == NULL ) continue;

    gtk_list_store_append (store, &iter);
    gtk_list_store_set(store,&iter,0,global_config->exclude_list->excludes[i],-1);
  }

  /* column for url */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Expression"), renderer,
    "text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);

}

static int populate_transaction_window(GtkWidget *trans_window)
{
  GtkTreeView *summary_treeview;
  GtkTreeStore *store;
  GtkTreeIter iter,child_iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkLabel *sum_pkg_num,*sum_dl_size,*sum_free_space;
  extern transaction_t *trans;
  extern rc_config *global_config;
  guint i;
  gint dl_size = 0,free_space = 0,already_dl_size = 0;
  gchar buf[512];

  summary_treeview = GTK_TREE_VIEW(lookup_widget(trans_window,"transaction_summary_treeview"));
  store = gtk_tree_store_new (1,G_TYPE_STRING);
  sum_pkg_num = GTK_LABEL(lookup_widget(trans_window,"summary_pkg_numbers"));
  sum_dl_size = GTK_LABEL(lookup_widget(trans_window,"summary_dl_size"));
  sum_free_space = GTK_LABEL(lookup_widget(trans_window,"summary_free_space"));

  /* setup the store */
  if ( trans->missing_err->err_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages with unmet dependencies"),-1);
    for (i=0; i < trans->missing_err->err_count; ++i) {
      unsigned int len = strlen(trans->missing_err->errs[i]->pkg) +
                                strlen(_(": Depends: ")) +
                                strlen(trans->missing_err->errs[i]->error) + 1;
      char *err = slapt_malloc(sizeof *err * len);
      snprintf(err,len,"%s: Depends: %s",trans->missing_err->errs[i]->pkg,
               trans->missing_err->errs[i]->error);
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,err,-1);
      free(err);
    }
  }
  if ( trans->conflict_err->err_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Package conflicts"),-1);
    for (i = 0; i < trans->conflict_err->err_count;++i) {
      unsigned int len = strlen(trans->conflict_err->errs[i]->error) +
                                strlen(_(", which is required by ")) +
                                strlen(trans->conflict_err->errs[i]->pkg) +
                                strlen(_(", is excluded")) + 1;
      char *err = slapt_malloc(sizeof *err * len);
      snprintf(err,len,"%s%s%s%s",
               trans->conflict_err->errs[i]->error,
               _(", which is required by "),
               trans->conflict_err->errs[i]->pkg,
               _(", is excluded"));
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,err,-1);
      free(err);
    }
  }
  if ( trans->exclude_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages excluded"),-1);
    for (i = 0; i < trans->exclude_pkgs->pkg_count;++i) {
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,trans->exclude_pkgs->pkgs[i]->name,-1);
    }
  }
  if ( trans->install_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be installed"),-1);
    for (i = 0; i < trans->install_pkgs->pkg_count;++i) {
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,trans->install_pkgs->pkgs[i]->name,-1);
      dl_size += trans->install_pkgs->pkgs[i]->size_c;
      already_dl_size += get_pkg_file_size(global_config,trans->install_pkgs->pkgs[i])/1024;
      free_space += trans->install_pkgs->pkgs[i]->size_u;
    }
  }
  if ( trans->upgrade_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be upgraded"),-1);
    for (i = 0; i < trans->upgrade_pkgs->pkg_count;++i) {
      gchar buf[255];
      buf[0] = '\0';
      strcat(buf,trans->upgrade_pkgs->pkgs[i]->upgrade->name);
      strcat(buf," (");
      strcat(buf,trans->upgrade_pkgs->pkgs[i]->installed->version);
      strcat(buf,") -> ");
      strcat(buf,trans->upgrade_pkgs->pkgs[i]->upgrade->version);
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,buf,-1);
      dl_size += trans->upgrade_pkgs->pkgs[i]->upgrade->size_c;
      already_dl_size += get_pkg_file_size(global_config,trans->upgrade_pkgs->pkgs[i]->upgrade)/1024;
      free_space += trans->upgrade_pkgs->pkgs[i]->upgrade->size_u;
      free_space -= trans->upgrade_pkgs->pkgs[i]->installed->size_u;
    }
  }
  if ( trans->remove_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be removed"),-1);
    for (i = 0; i < trans->remove_pkgs->pkg_count;++i) {
      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,trans->remove_pkgs->pkgs[i]->name,-1);
      free_space -= trans->remove_pkgs->pkgs[i]->size_u;
    }
  }
  gtk_tree_view_set_model (GTK_TREE_VIEW(summary_treeview),GTK_TREE_MODEL(store));

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes (_("Package"), renderer,
    "text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(summary_treeview), column);

  snprintf(buf,512,_("%d upgraded, %d newly installed, %d to remove and %d not upgraded."),
    trans->upgrade_pkgs->pkg_count,
    trans->install_pkgs->pkg_count,
    trans->remove_pkgs->pkg_count,
    trans->exclude_pkgs->pkg_count
  );
  gtk_label_set_text(GTK_LABEL(sum_pkg_num),buf);

  /* if we don't have enough free space */
  if ( disk_space(global_config,dl_size - already_dl_size + free_space) != 0 ) {
    notify(_("Error"),_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
    return -1;
  }

  if ( already_dl_size > 0 ) {
    int need_to_dl = dl_size - already_dl_size;
    snprintf(buf,512,_("Need to get %0.1d%s/%0.1d%s of archives.\n"),
      (need_to_dl > 1024 ) ? need_to_dl / 1024
        : need_to_dl,
      (need_to_dl > 1024 ) ? "MB" : "kB",
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,_("Need to get %0.1d%s of archives."),
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }
  gtk_label_set_text(GTK_LABEL(sum_dl_size),buf);

  if ( free_space < 0 ) {
    free_space *= -1;
    snprintf(buf,512,_("After unpacking %0.1d%s disk space will be freed."),
      (free_space > 1024 ) ? free_space / 1024
        : free_space,
      (free_space > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,_("After unpacking %0.1d%s of additional disk space will be used."),
      (free_space > 1024 ) ? free_space / 1024 : free_space,
        (free_space > 1024 ) ? "MB" : "kB"
    );
  }
  gtk_label_set_text(GTK_LABEL(sum_free_space),buf);

  return 0;
}


void clear_button_clicked(GtkWidget *w, gpointer user_data) 
{
  const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(w));

  /* only rebuild if we have to, costly */
  if ( strcmp(search_text,"") != 0 ) {
    gtk_entry_set_text(GTK_ENTRY(w),"");
    rebuild_treeviews();
  } else {
    gtk_entry_set_text(GTK_ENTRY(w),"");
  }

}

static void build_upgrade_list(void)
{
  extern rc_config *global_config;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;
  extern GtkWidget *gslapt;
  guint i;

  for (i = 0; i < installed->pkg_count;i++) {
    pkg_info_t *update_pkg = NULL;
    pkg_info_t *newer_installed_pkg = NULL;

    /*
      we need to see if there is another installed
      package that is newer than this one
    */
    if ( (newer_installed_pkg = get_newest_pkg(installed,installed->pkgs[i]->name)) != NULL ) {

      if ( cmp_pkg_versions(installed->pkgs[i]->version,newer_installed_pkg->version) < 0 ) continue;

    }

    /* see if we have an available update for the pkg */
    update_pkg = get_newest_pkg(
      all,
      installed->pkgs[i]->name
    );
    if ( update_pkg != NULL ) {
      int cmp_r = 0;

      /* if the update has a newer version, attempt to upgrade */
      cmp_r = cmp_pkg_versions(installed->pkgs[i]->version,update_pkg->version);
      /* either it's greater, or we want to reinstall */
      if ( cmp_r < 0 || (global_config->re_install == TRUE) ) {

        if ( (is_excluded(global_config,update_pkg) == 1)
          || (is_excluded(global_config,installed->pkgs[i]) == 1)
        ) {
          add_exclude_to_transaction(trans,update_pkg);
        }else{
          /* if all deps are added and there is no conflicts, add on */
          if (
            (add_deps_to_trans(global_config,trans,all,installed,update_pkg) == 0)
            && ( is_conflicted(trans,all,installed,update_pkg) == NULL )
          ) {
            add_upgrade_to_transaction(trans,installed->pkgs[i],update_pkg);
          }else{
            /* otherwise exclude */
            add_exclude_to_transaction(trans,update_pkg);
          }
        }

      }

    }/* end upgrade pkg found */

  }/* end for */

}

static gboolean download_packages(void)
{
  GtkLabel *progress_action_label,*progress_message_label,*progress_pkg_desc;
  GtkProgressBar *p_bar;
  extern transaction_t *trans;
  extern rc_config *global_config;
  guint i,context_id;
  gfloat pkgs_to_dl = 0.0,count = 0.0;

  pkgs_to_dl += trans->install_pkgs->pkg_count;
  pkgs_to_dl += trans->upgrade_pkgs->pkg_count;

  progress_window = create_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),_("Progress"));

  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  gtk_widget_show(progress_window);
  context_id = gslapt_set_status(_("Downloading packages..."));
  gdk_threads_leave();

  for (i = 0; i < trans->install_pkgs->pkg_count;++i) {

    guint msg_len = strlen(trans->install_pkgs->pkgs[i]->name)
        + strlen("-") + strlen(trans->install_pkgs->pkgs[i]->version)
        + strlen(".") + strlen(".tgz");
    gchar *msg = slapt_malloc(msg_len * sizeof *msg);

    snprintf(msg,
      strlen(trans->install_pkgs->pkgs[i]->name)
      + strlen("-")
      + strlen(trans->install_pkgs->pkgs[i]->version)
      + strlen(".") + strlen(".tgz"),
      "%s-%s.tgz",
      trans->install_pkgs->pkgs[i]->name,
      trans->install_pkgs->pkgs[i]->version
    );

    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->install_pkgs->pkgs[i]->description);
    gtk_label_set_text(progress_action_label,_("Downloading..."));
    gtk_label_set_text(progress_message_label,msg);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/pkgs_to_dl)/100);
    gdk_threads_leave();

    free(msg);

    if ( download_pkg(global_config,trans->install_pkgs->pkgs[i]) == -1) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return FALSE;
    }
    ++count;
  }
  for (i = 0; i < trans->upgrade_pkgs->pkg_count;++i) {

    guint msg_len = strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->name)
        + strlen("-") + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->version)
        + strlen(".") + strlen(".tgz");
    gchar *msg = slapt_malloc( sizeof *msg * msg_len);

    snprintf(msg,
      strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->name)
      + strlen("-")
      + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->version)
      + strlen(".") + strlen(".tgz"),
      "%s-%s.tgz",
      trans->upgrade_pkgs->pkgs[i]->upgrade->name,
      trans->upgrade_pkgs->pkgs[i]->upgrade->version
    );

    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->upgrade_pkgs->pkgs[i]->upgrade->description);
    gtk_label_set_text(progress_action_label,_("Downloading..."));
    gtk_label_set_text(progress_message_label,msg);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/pkgs_to_dl)/100);
    gdk_threads_leave();

    free(msg);

    if (download_pkg(global_config,trans->upgrade_pkgs->pkgs[i]->upgrade) == -1) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return FALSE;
    }
    ++count;
  }

  gdk_threads_enter();
  gtk_widget_destroy(progress_window);
  gslapt_clear_status(context_id);
  gdk_threads_leave();

  return TRUE;
}

static gboolean install_packages(void)
{
  GtkLabel *progress_action_label,*progress_message_label,*progress_pkg_desc;
  GtkProgressBar *p_bar;
  extern transaction_t *trans;
  extern rc_config *global_config;
  guint i,context_id;
  gfloat count = 0.0;

  /* begin removing, installing, and upgrading */

  progress_window = create_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),_("Progress"));

  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  gtk_widget_show(progress_window);
  context_id = gslapt_set_status(_("Removing packages..."));
  gdk_threads_leave();

  for (i = 0; i < trans->remove_pkgs->pkg_count;++i) {
    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->remove_pkgs->pkgs[i]->description);
    gtk_label_set_text(progress_action_label,_("Uninstalling..."));
    gtk_label_set_text(progress_message_label,trans->remove_pkgs->pkgs[i]->name);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->remove_pkgs->pkg_count)/100);
    gdk_threads_leave();

    if ( remove_pkg(global_config,trans->remove_pkgs->pkgs[i]) == -1 ) {
      gtk_widget_destroy(progress_window);
      return FALSE;
    }
    ++count;
  }

  /* reset progress bar */
  gdk_threads_enter();
  gtk_progress_bar_set_fraction(p_bar,0.0);
  gslapt_clear_status(context_id);
  context_id = gslapt_set_status(_("Installing packages..."));
  gdk_threads_leave();

  /* now for the installs and upgrades */
  count = 0.0;
  for (i = 0;i < trans->queue->count; ++i) {
    if ( trans->queue->pkgs[i]->type == INSTALL ) {
      gdk_threads_enter();
      gtk_label_set_text(progress_pkg_desc,trans->queue->pkgs[i]->pkg.i->description);
      gtk_label_set_text(progress_action_label,_("Installing..."));
      gtk_label_set_text(progress_message_label,trans->queue->pkgs[i]->pkg.i->name);
      gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->queue->count)/100);
      gdk_threads_leave();

      if ( install_pkg(global_config,trans->queue->pkgs[i]->pkg.i) == -1 ) {
        gtk_widget_destroy(progress_window);
        return FALSE;
      }
    }else if ( trans->queue->pkgs[i]->type == UPGRADE ) {
      gdk_threads_enter();
      gtk_label_set_text(progress_pkg_desc,trans->queue->pkgs[i]->pkg.u->upgrade->description);
      gtk_label_set_text(progress_action_label,_("Upgrading..."));
      gtk_label_set_text(progress_message_label,trans->queue->pkgs[i]->pkg.u->upgrade->name);
      gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->queue->count)/100);
      gdk_threads_leave();

      if ( upgrade_pkg(global_config,trans->queue->pkgs[i]->pkg.u->installed,trans->queue->pkgs[i]->pkg.u->upgrade) == -1 ) {
        gtk_widget_destroy(progress_window);
        return FALSE;
      }
    }
    ++count;
  }

  gdk_threads_enter();
  gtk_widget_destroy(progress_window);
  gslapt_clear_status(context_id);
  gdk_threads_leave();

  return TRUE;
}


void clean_callback(GtkMenuItem *menuitem, gpointer user_data)
{
  GThread *gpd;
  extern rc_config *global_config;

  gpd = g_thread_create((GThreadFunc)clean_pkg_dir,global_config->working_dir,FALSE,NULL);

}


void preferences_sources_add(GtkWidget *w, gpointer user_data)
{
  extern rc_config *global_config;
  GtkTreeView *source_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_sources_treeview"));
  GtkEntry *new_source_entry = GTK_ENTRY(lookup_widget(w,"new_source_entry"));
  const gchar *new_source = gtk_entry_get_text(new_source_entry);
  guint i;

  if ( new_source == NULL || strlen(new_source) < 1 ) return;

  add_source(global_config->sources,new_source);

  gtk_entry_set_text(new_source_entry,"");
  clear_treeview(source_tree);
  build_sources_treeviewlist((GtkWidget *)source_tree,global_config);

}

void preferences_sources_remove(GtkWidget *w, gpointer user_data)
{
  extern rc_config *global_config;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *source_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_sources_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (source_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    guint i = 0;
    gchar *source;
    gchar *tmp = NULL;

    gtk_tree_model_get(model,&iter,0,&source, -1 );

    clear_treeview(source_tree);

    while ( i < global_config->sources->count ) {
      if ( strcmp(source,global_config->sources->url[i]) == 0 && tmp == NULL ) {
        tmp = global_config->sources->url[i];
      }
      if ( tmp != NULL && (i+1 < global_config->sources->count) ) {
        global_config->sources->url[i] = global_config->sources->url[i + 1];
      }
      ++i;
    }
    if ( tmp != NULL ) {
      char **realloc_tmp;
      int count = global_config->sources->count - 1;
      if ( count < 1 ) count = 1;

      free(tmp);

      realloc_tmp = realloc(global_config->sources->url,sizeof *global_config->sources->url * count );
      if ( realloc_tmp != NULL ) {
        global_config->sources->url = realloc_tmp;
        if ( global_config->sources->count > 0 ) --global_config->sources->count;
      }

    }

    g_free(source);

    build_sources_treeviewlist((GtkWidget *)source_tree,global_config);
  }

}

void preferences_on_ok_clicked(GtkWidget *w, gpointer user_data)
{
  extern rc_config *global_config;
  GtkEntry *preferences_working_dir_entry = GTK_ENTRY(lookup_widget(w,"preferences_working_dir_entry"));
  const gchar *working_dir = gtk_entry_get_text(preferences_working_dir_entry);

  strncpy(
    global_config->working_dir,
    working_dir,
    strlen(working_dir)
  );
  global_config->working_dir[
    strlen(working_dir)
  ] = '\0';

  if ( write_preferences() == FALSE ) {
    fprintf(stderr,"Failed to commit preferences\n");
    on_gslapt_destroy(NULL,NULL);
  }

  gtk_widget_destroy(w);
}


void preferences_exclude_add(GtkWidget *w, gpointer user_data) 
{
  extern rc_config *global_config;
  GtkTreeView *exclude_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_exclude_treeview"));
  GtkEntry *new_exclude_entry = GTK_ENTRY(lookup_widget(w,"new_exclude_entry"));
  const gchar *new_exclude = gtk_entry_get_text(new_exclude_entry);
  char **tmp_realloc = NULL;

  if ( new_exclude == NULL || strlen(new_exclude) < 1 ) return;

  tmp_realloc = realloc(global_config->exclude_list->excludes,
    sizeof *global_config->exclude_list->excludes * (global_config->exclude_list->count + 1)
  );
  if ( tmp_realloc != NULL ) {
    char *ne = strndup(new_exclude,strlen(new_exclude));
    global_config->exclude_list->excludes = tmp_realloc;
    global_config->exclude_list->excludes[global_config->exclude_list->count] = ne;
    ++global_config->exclude_list->count;
  }

  gtk_entry_set_text(new_exclude_entry,"");
  clear_treeview(exclude_tree);
  build_exclude_treeviewlist((GtkWidget *)exclude_tree,global_config);

}


void preferences_exclude_remove(GtkWidget *w, gpointer user_data) 
{
  extern rc_config *global_config;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *exclude_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_exclude_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (exclude_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    guint i = 0;
    gchar *tmp = NULL;
    gchar *exclude;

    gtk_tree_model_get(model,&iter,0,&exclude, -1 );

    clear_treeview(exclude_tree);

    while (i < global_config->exclude_list->count) {
      if ( strcmp(exclude,global_config->exclude_list->excludes[i]) == 0 && tmp == NULL ) {
        tmp = global_config->exclude_list->excludes[i];
      }
      if ( tmp != NULL && (i+1 < global_config->exclude_list->count) ) {
        global_config->exclude_list->excludes[i] = global_config->exclude_list->excludes[i + 1];
      }
      ++i;
    }
    if ( tmp != NULL ) {
      char **realloc_tmp;
      int count = global_config->exclude_list->count - 1;
      if ( count < 1 ) count = 1;

      free(tmp);

      realloc_tmp = realloc(  
        global_config->exclude_list->excludes,
        sizeof *global_config->exclude_list->excludes * count
      );
      if ( realloc_tmp != NULL ) {
        global_config->exclude_list->excludes = realloc_tmp;
        if ( global_config->exclude_list->count > 0 ) --global_config->exclude_list->count;
      }

    }

    g_free(exclude);

    build_exclude_treeviewlist((GtkWidget *)exclude_tree,global_config);
  }

}

static gboolean write_preferences(void)
{
  extern rc_config *global_config;
  guint i;
  FILE *rc;

  rc = open_file(RC_LOCATION,"w+");
  if ( rc == NULL ) return FALSE;

  fprintf(rc,"%s%s\n",WORKINGDIR_TOKEN,global_config->working_dir);

  fprintf(rc,"%s",EXCLUDE_TOKEN);
  for (i = 0;i < global_config->exclude_list->count;++i) {
    if ( i+1 == global_config->exclude_list->count) {
      fprintf(rc,"%s",global_config->exclude_list->excludes[i]);
    }else{
      fprintf(rc,"%s,",global_config->exclude_list->excludes[i]);
    }
  }
  fprintf(rc,"\n");

  for (i = 0; i < global_config->sources->count;++i) {
    fprintf(rc,"%s%s\n",SOURCE_TOKEN,global_config->sources->url[i]);
  }

  fclose(rc);

  return TRUE;
}


void cancel_preferences(GtkWidget *w, gpointer user_data)
{
  extern rc_config *global_config;
  gtk_widget_destroy(w);
  free_rc_config(global_config);
  global_config = read_rc_config(RC_LOCATION);
}


void cancel_transaction(GtkWidget *w, gpointer user_data)
{
  gtk_widget_destroy(w);
}

void cancel_upgrade_transaction(GtkWidget *w,gpointer user_data)
{
  extern transaction_t *trans;
  extern GtkWidget *gslapt;

  free_transaction(trans);
  init_transaction(trans);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"), FALSE);
  rebuild_treeviews();

  gtk_widget_destroy(w);

}

void add_pkg_for_reinstall (GtkWidget *gslapt, gpointer user_data)
{
  extern rc_config *global_config;

  global_config->re_install = TRUE;
  add_pkg_for_install(gslapt,user_data);
  global_config->re_install = FALSE;

}

static void set_execute_active(void)
{
  extern GtkWidget *gslapt;

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"), TRUE);

  if ( pending_trans_context_id == 0 ) {
    pending_trans_context_id = gslapt_set_status(_("Pending changes. Click execute when ready."));
  }

}

static void clear_execute_active(void)
{
  extern GtkWidget *gslapt;

  if ( pending_trans_context_id > 0 ) {
    gtk_statusbar_pop(
      GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar")),
      pending_trans_context_id
    );
    pending_trans_context_id = 0;
  }

}

static void notify(const char *title,const char *message)
{
  GtkWidget *w = create_notification();
  gtk_window_set_title (GTK_WINDOW (w), title);
  gtk_label_set_text(GTK_LABEL(lookup_widget(w,"notification_label")),message);
  gtk_label_set_use_markup (GTK_LABEL(lookup_widget(w,"notification_label")),
                            TRUE);
  gtk_widget_show(w);
}

static int disk_space(const rc_config *global_config,int space_needed )
{
  struct statvfs statvfs_buf;

  space_needed *= 1024;

  if ( space_needed < 0 ) return 0;

  if ( statvfs(global_config->working_dir,&statvfs_buf) != 0 ) {
    if ( errno ) perror("statvfs");
    return 1;
  } else {
    if ( statvfs_buf.f_bfree < ( space_needed / statvfs_buf.f_bsize) )
      return 1;
  }

  return 0;
}

static void pkg_action_popup_menu(GtkTreeView *treeview, gpointer data)
{
  GtkWidget *menu, *install = NULL, *remove, *image;
  GdkEventButton *event = gtk_get_current_event();
  GtkTreeViewColumn *column;
  GtkTreePath *path;
  GtkTreeSelection *select;
  GtkTreeIter iter;
  GtkTreeModel *model;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  extern transaction_t *trans;
  extern rc_config *global_config;
  extern GtkWidget *gslapt;
  pkg_info_t *pkg = NULL, *newest_installed = NULL, *upgrade_pkg = NULL;
  guint is_installed = 0,is_newest = 1,is_exclude = 0,is_downloadable = 0,is_downgrade = 0;

  if (event->type != GDK_BUTTON_PRESS)
    return;

  if ( !gtk_tree_view_get_path_at_pos(treeview,event->x,event->y,&path,&column,NULL,NULL) )
    return;

  if (event->button != 3 && (event->button == 1 && strcmp(column->title,_("Status")) != 0))
    return;

  gtk_tree_path_free(path);

  /* get selection */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (gtk_tree_selection_get_selected(select,&model,&iter)) {
    gchar *p_name,*p_version,*p_location;

    gtk_tree_model_get (model, &iter, 1, &p_name, -1);
    gtk_tree_model_get (model, &iter, 2, &p_version, -1);
    gtk_tree_model_get (model, &iter, 3, &p_location, -1);

    pkg = get_pkg_by_details(all,p_name,p_version,p_location);

    if (pkg == NULL) {
      pkg = get_pkg_by_details(installed,p_name,p_version,p_location);
      is_installed = 1;
    }

    g_free (p_name);
    g_free (p_version);
    g_free (p_location);


    if ( pkg == NULL ) {
      return;
    }

    if (is_installed != 1 &&
      get_exact_pkg(installed,pkg->name,pkg->version) != NULL
    ) {
      is_installed = 1;
    }

    if ( get_pkg_by_details(all,pkg->name,pkg->version,pkg->location) != NULL) {
      is_downloadable = 1;
    }

    newest_installed = get_newest_pkg(installed,pkg->name);
    if ( newest_installed != NULL && cmp_pkg_versions(pkg->version,newest_installed->version) < 0 ) {
      is_downgrade = 1;
    }

    upgrade_pkg = get_newest_pkg(all,pkg->name);
    if ( upgrade_pkg != NULL && cmp_pkg_versions(pkg->version,upgrade_pkg->version) < 0 ) {
      is_newest = 0;
    }

    if ( is_excluded(global_config,pkg) == 1 
    || get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) != NULL) {
      is_exclude = 1;
    }

  }

  menu = gtk_menu_new ();
  gtk_widget_show (menu);

  if ( search_transaction(trans,pkg->name) == 0 ) {
    if ( is_exclude == 0 ) {
      /* upgrade */
      if ( is_installed == 1 && is_newest == 0 && (search_transaction_by_pkg(trans,upgrade_pkg) == 0 ) ) {
        install = gtk_image_menu_item_new_with_mnemonic(_("Upgrade"));
        g_signal_connect_swapped(G_OBJECT (install), "activate",
          G_CALLBACK (add_pkg_for_install), GTK_WIDGET(gslapt));
      /* re-install */
      }else if ( is_installed == 1 && is_newest == 1 && is_downloadable == 1 ) {
        install = gtk_image_menu_item_new_with_mnemonic(_("Re-Install"));
        g_signal_connect_swapped((gpointer) install,"activate",
          G_CALLBACK(add_pkg_for_reinstall),GTK_OBJECT(gslapt));
      /* this is for downgrades */
      }else if ( is_installed == 0 && is_downgrade == 1 && is_downloadable == 1 ) {
        install = gtk_image_menu_item_new_with_mnemonic(_("Downgrade"));
        g_signal_connect_swapped((gpointer) install,"activate",
          G_CALLBACK(add_pkg_for_reinstall),GTK_OBJECT(gslapt));
      /* straight up install */
      }else if ( is_installed == 0 && is_downloadable == 1 ) {
        install = gtk_image_menu_item_new_with_mnemonic(_("Install"));
        g_signal_connect_swapped(G_OBJECT (install), "activate",
          G_CALLBACK (add_pkg_for_install), GTK_WIDGET(gslapt));
      }
    }

  }

  if ( install == NULL) {
    install = gtk_image_menu_item_new_with_mnemonic(_("Install"));
    gtk_widget_set_sensitive(GTK_WIDGET(install),FALSE);
  }

  gtk_widget_show (install);
  gtk_container_add (GTK_CONTAINER (menu), install);
  image = gtk_image_new_from_stock ("gtk-add", GTK_ICON_SIZE_MENU);
  gtk_widget_show (image);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (install), image);

  remove = gtk_image_menu_item_new_with_mnemonic (_("Remove"));
  gtk_widget_show (remove);
  gtk_container_add (GTK_CONTAINER (menu), remove);
  image = gtk_image_new_from_stock ("gtk-remove", GTK_ICON_SIZE_MENU);
  gtk_widget_show (image);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (remove), image);
  gtk_widget_set_sensitive(GTK_WIDGET(remove),FALSE);

  g_signal_connect_swapped(G_OBJECT (remove), "activate",
    G_CALLBACK (add_pkg_for_removal), GTK_WIDGET(gslapt));

  if (
    is_installed == 1 && is_exclude != 1 &&
    (search_transaction(trans,pkg->name) == 0 )
  ) {
    gtk_widget_set_sensitive( GTK_WIDGET(remove),TRUE);
  }

  
  gtk_menu_popup(
    menu,
   NULL,
   NULL,
   NULL,
   NULL,
   event->button,
   gtk_get_current_event_time()
  );

}

