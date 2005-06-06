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
static guint _cancelled = 0;
static guint pending_trans_context_id = 0;
static int disk_space(const rc_config *global_config,int space_needed );
static gboolean pkg_action_popup_menu(GtkTreeView *treeview, gpointer data);
static int set_iter_to_pkg(GtkTreeModel *model, GtkTreeIter *iter,
                           pkg_info_t *pkg);
static void reset_pkg_view_status(void);
static int lsearch_upgrade_transaction(transaction_t *tran,pkg_info_t *pkg);
static void build_package_action_menu(pkg_info_t *pkg);
static void rebuild_package_action_menu(void);
static void mark_upgrade_packages(void);
static void fillin_pkg_details(pkg_info_t *pkg);
static void clear_treeview(GtkTreeView *treeview);
static void get_package_data(void);
static void rebuild_treeviews(void);
static guint gslapt_set_status(const gchar *);
static void gslapt_clear_status(guint context_id);
static void lock_toolbar_buttons(void);
static void unlock_toolbar_buttons(void);
static void build_sources_treeviewlist(GtkWidget *treeview,
                                       const rc_config *global_config);
static void build_exclude_treeviewlist(GtkWidget *treeview,
                                       const rc_config *global_config);
static int populate_transaction_window(GtkWidget *trans_window);
static gboolean download_packages(void);
static gboolean install_packages(void);
static gboolean write_preferences(void);
static void set_execute_active(void);
static void clear_execute_active(void);
static void notify(const char *title,const char *message);

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
  extern transaction_t *trans;
  mark_upgrade_packages();
  if (trans->install_pkgs->pkg_count > 0 || trans->upgrade_pkgs->pkg_count > 0) {
    set_execute_active();
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
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));

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
  GtkBin *caller_button = (GtkBin *)user_data;
  GtkLabel *caller_button_label = GTK_LABEL(gtk_bin_get_child(caller_button));

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);
    gtk_tree_model_get (model, &iter, 3, &pkg_location, -1);

    if ( pkg_name == NULL || pkg_version == NULL || pkg_location == NULL) {
      fprintf(stderr,"failed to get package name and version from selection\n");

      if (pkg_name != NULL)
        g_free(pkg_name);

      if (pkg_version != NULL)
        g_free(pkg_version);

      if (pkg_location != NULL)
        g_free(pkg_location);

      return;
    }

    if ( strcmp(gtk_label_get_text(caller_button_label),(gchar *)_("Upgrade")) == 0) {
      pkg = get_newest_pkg(all,pkg_name);
    } else {
      pkg = get_pkg_by_details(all,pkg_name,pkg_version,pkg_location);
    }

    if ( pkg == NULL ) {
      fprintf(stderr,"Failed to find package: %s-%s@%s\n",pkg_name,pkg_version,pkg_location);
      g_free(pkg_name);
      g_free(pkg_version);
      g_free(pkg_location);
      return;
    }

    installed_pkg = get_newest_pkg(installed,pkg_name);

    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

  }

  if ( pkg == NULL ) {
    fprintf(stderr,"No package to work with\n");
    return;
  }

  /* if it is not already installed, install it */
  if ( installed_pkg == NULL ) {

    if ( ladd_deps_to_trans(global_config,trans,all,installed,pkg) == 0 ) {
      pkg_info_t *conflicted_pkg = NULL;
      gchar *status = NULL;

      add_install_to_transaction(trans,pkg);
      status = g_strdup_printf("i%s",pkg->name);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_install.png"),-1);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
      set_execute_active();
      g_free(status);

      /* if there is a conflict, we schedule the conflict for removal */
      if ( (conflicted_pkg = is_conflicted(trans,all,installed,pkg)) != NULL ) {
        add_remove_to_transaction(trans,conflicted_pkg);
        set_execute_active();
        if (set_iter_to_pkg(model,&iter,conflicted_pkg)) {
          gchar *rstatus = g_strdup_printf("r%s",conflicted_pkg->name);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_remove.png"),-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,rstatus,-1);
          g_free(rstatus);
        }
      }

    }else{
      gchar *msg = g_strdup_printf("Excluding %s due to dependency failure\n",pkg->name);
      notify((gchar *)_("Error"),msg);
      add_exclude_to_transaction(trans,pkg);
      g_free(msg);
    }

  }else{ /* else we upgrade or reinstall */
     int ver_cmp;

    /* it is already installed, attempt an upgrade */
    if (
      ((ver_cmp = cmp_pkg_versions(installed_pkg->version,pkg->version)) < 0) ||
      (global_config->re_install == TRUE)
    ) {

      if ( ladd_deps_to_trans(global_config,trans,all,installed,pkg) == 0 ) {
        pkg_info_t *conflicted_pkg = NULL;

        if ( (conflicted_pkg = is_conflicted(trans,all,installed,pkg)) != NULL ) {
          fprintf(stderr,"%s conflicts with %s\n",pkg->name,conflicted_pkg->name);
          add_remove_to_transaction(trans,conflicted_pkg);
          set_execute_active();
          if (set_iter_to_pkg(model,&iter,conflicted_pkg)) {
            gchar *rstatus = g_strdup_printf("r%s",conflicted_pkg->name);
            gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_remove.png"),-1);
            gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,rstatus,-1);
            g_free(rstatus);
          }
        }else{
          add_upgrade_to_transaction(trans,installed_pkg,pkg);
          if (global_config->re_install == TRUE) {
            if (ver_cmp == 0) {
              gchar *status = g_strdup_printf("u%s",pkg->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_reinstall.png"),-1);
              g_free(status);
            } else {
              gchar *status = g_strdup_printf("u%s",pkg->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_downgrade.png"),-1);
              g_free(status);
              if (set_iter_to_pkg(model,&iter,installed_pkg)) {
                gchar *ustatus = g_strdup_printf("u%s",installed_pkg->name);
                gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_downgrade.png"),-1);
                gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,ustatus,-1);
                g_free(ustatus);
              }
            }
          } else {
            pkg_info_t *inst_avail = get_exact_pkg(all,installed_pkg->name,installed_pkg->version);
            gchar *status = g_strdup_printf("u%s",pkg->name);
            gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
            gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
            g_free(status);
            if ( pkg != NULL && set_iter_to_pkg(model,&iter,pkg)) {
              gchar *ustatus = g_strdup_printf("u%s",pkg->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,ustatus,-1);
              g_free(ustatus);
            }
            if (installed_pkg != NULL && set_iter_to_pkg(model,&iter,installed_pkg)) {
              gchar *ustatus = g_strdup_printf("u%s",installed_pkg->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,ustatus,-1);
              g_free(ustatus);
            }
            if (inst_avail != NULL && set_iter_to_pkg(model,&iter,inst_avail)) {
              gchar *ustatus = g_strdup_printf("u%s",inst_avail->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,ustatus,-1);
              g_free(ustatus);
            }
          }
          set_execute_active();
        }

      }else{
        gchar *msg = g_strdup_printf("Excluding %s due to dependency failure\n",pkg->name);
        notify((gchar *)_("Error"),msg);
        add_exclude_to_transaction(trans,pkg);
        g_free(msg);
      }

    }

  }

  rebuild_package_action_menu();
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
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    pkg_info_t *pkg;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);
    gtk_tree_model_get (model, &iter, 3, &pkg_location, -1);

    /*
      can't use get_pkg_by_details() as the location field will be different
      in the available package and the installed package
    */
    if ( (pkg = get_exact_pkg(installed,pkg_name,pkg_version)) != NULL ) {
      guint c;
      struct pkg_list *deps;
      gchar *status = NULL;

      deps = is_required_by(global_config,installed,pkg);

      /*
        this solves the problem where an available package that is installed
        cannot be unmarked b/c the installed package has a different location
        than the available package (even though we treat them the same)
      */
      free(pkg->location);
      pkg->location = g_strdup(pkg_location);

      add_remove_to_transaction(trans,pkg);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_remove.png"),-1);
      status = g_strdup_printf("r%s",pkg->name);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
      g_free(status);
      set_execute_active();

      for (c = 0; c < deps->pkg_count;c++) {
        add_remove_to_transaction(trans,deps->pkgs[c]);
        if (set_iter_to_pkg(model,&iter,deps->pkgs[c])) {
          gchar *status = g_strdup_printf("u%s",deps->pkgs[c]->name);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_remove.png"),-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
          g_free(status);
        }
      }

      free(deps->pkgs);
      free(deps);

    }

    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

  }

  rebuild_package_action_menu();

}

void build_package_treeviewlist(GtkWidget *treeview)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkTreeViewColumn *column;
  GtkWidget *image;
  guint i = 0;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;
  extern GtkWidget *gslapt;

  store = gtk_list_store_new (
    NUMBER_OF_COLUMNS,
    GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
  );

  for (i = 0; i < all->pkg_count; i++ ) {
    /* we use this for sorting the status */
    /* a=installed,i=install,r=remove,u=upgrade,z=available */
    gchar *status = NULL;
    guint is_inst = 0;
    GdkPixbuf *status_icon = NULL;
    gchar *short_desc = gen_short_pkg_description(all->pkgs[i]);

    if (get_exact_pkg(installed,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      is_inst = 1;
    }

    if (trans->exclude_pkgs->pkg_count > 0 &&
    get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_available.png");
      status = g_strdup_printf("z%s",all->pkgs[i]->name);
    } else if (trans->remove_pkgs->pkg_count > 0 &&
    get_exact_pkg(trans->remove_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_remove.png");
      status = g_strdup_printf("r%s",all->pkgs[i]->name);
    } else if (trans->install_pkgs->pkg_count > 0 &&
    get_exact_pkg(trans->install_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_install.png");
      status = g_strdup_printf("i%s",all->pkgs[i]->name);
    } else if (trans->upgrade_pkgs->pkg_count > 0 && lsearch_upgrade_transaction(trans,all->pkgs[i]) == 1) {
      status_icon = create_pixbuf("pkg_action_upgrade.png");
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
      STATUS_ICON_COLUMN,status_icon,
      NAME_COLUMN,all->pkgs[i]->name,
      VERSION_COLUMN,all->pkgs[i]->version,
      LOCATION_COLUMN,all->pkgs[i]->location,
      DESC_COLUMN,short_desc,
      STATUS_COLUMN,status,
      -1
    );

    g_free(status);
    g_free(short_desc);
  }

  for (i = 0; i < installed->pkg_count; ++i) {
    if ( get_exact_pkg(all,installed->pkgs[i]->name,installed->pkgs[i]->version) == NULL ) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      GdkPixbuf *status_icon = NULL;
      gchar *short_desc = gen_short_pkg_description(installed->pkgs[i]);

      if (trans->remove_pkgs->pkg_count > 0 &&
      get_exact_pkg(trans->remove_pkgs,installed->pkgs[i]->name,installed->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",installed->pkgs[i]->name);
      } else {
        status_icon = create_pixbuf("pkg_action_installed.png");
        status = g_strdup_printf("a%s",installed->pkgs[i]->name);
      }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set ( store, &iter,
        STATUS_ICON_COLUMN,status_icon,
        NAME_COLUMN,installed->pkgs[i]->name,
        VERSION_COLUMN,installed->pkgs[i]->version,
        LOCATION_COLUMN,installed->pkgs[i]->location,
        DESC_COLUMN,short_desc,
        STATUS_COLUMN,status,
        -1
      );

      g_free(status);
      g_free(short_desc);
    }
  }

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

  gdk_window_set_cursor(gslapt->window,NULL);
}


void build_searched_treeviewlist(GtkWidget *treeview, gchar *pattern)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GdkPixbuf *image;
  guint i = 0;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;

  store = gtk_list_store_new (
    NUMBER_OF_COLUMNS,
    GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
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
      gchar *short_desc = gen_short_pkg_description(a_matches->pkgs[i]);

      if (get_exact_pkg(installed,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        is_inst = 1;
      }

      if (trans->exclude_pkgs->pkg_count > 0 &&
      get_exact_pkg(trans->exclude_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_available.png");
        status = g_strdup_printf("z%s",a_matches->pkgs[i]->name);
      } else if (trans->remove_pkgs->pkg_count > 0 &&
      get_exact_pkg(trans->remove_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",a_matches->pkgs[i]->name);
      } else if (trans->install_pkgs->pkg_count > 0 &&
      get_exact_pkg(trans->install_pkgs,a_matches->pkgs[i]->name,a_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_install.png");
        status = g_strdup_printf("i%s",a_matches->pkgs[i]->name);
      } else if (trans->upgrade_pkgs->pkg_count > 0 && lsearch_upgrade_transaction(trans,a_matches->pkgs[i]) == 1) {
        status_icon = create_pixbuf("pkg_action_upgrade.png");
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
        STATUS_ICON_COLUMN,status_icon,
        NAME_COLUMN,a_matches->pkgs[i]->name,
        VERSION_COLUMN,a_matches->pkgs[i]->version,
        LOCATION_COLUMN,a_matches->pkgs[i]->location,
        DESC_COLUMN,short_desc,
        STATUS_COLUMN,status,
        -1
      );
      g_free(status);
      g_free(short_desc);
    }

    i_matches = search_pkg_list(installed,pattern);
    for (i = 0; i < i_matches->pkg_count; i++ ) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      GdkPixbuf *status_icon = NULL;
      gchar *short_desc = gen_short_pkg_description(i_matches->pkgs[i]);

      if ( get_exact_pkg( a_matches, i_matches->pkgs[i]->name,
                          i_matches->pkgs[i]->version) != NULL ) {
         continue;
      }

      if (trans->remove_pkgs->pkg_count > 0 &&
      get_exact_pkg(trans->remove_pkgs,i_matches->pkgs[i]->name,i_matches->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",i_matches->pkgs[i]->name);
      } else {
        status_icon = create_pixbuf("pkg_action_installed.png");
        status = g_strdup_printf("a%s",i_matches->pkgs[i]->name);
      }

      gtk_list_store_append (store, &iter);
      gtk_list_store_set ( store, &iter,
        STATUS_ICON_COLUMN,status_icon,
        NAME_COLUMN,i_matches->pkgs[i]->name,
        VERSION_COLUMN,i_matches->pkgs[i]->version,
        LOCATION_COLUMN,i_matches->pkgs[i]->location,
        DESC_COLUMN,short_desc,
        STATUS_COLUMN,status,
        -1
      );

      g_free(status);
      g_free(short_desc);
    }
    free_pkg_list(a_matches);
    free_pkg_list(i_matches);

  }

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

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
    if (pkg != NULL) {
      fillin_pkg_details(pkg);
      build_package_action_menu(pkg);
    }else{
      pkg = get_pkg_by_details(installed,p_name,p_version,p_location);
      if (pkg != NULL) {
        fillin_pkg_details(pkg);
        build_package_action_menu(pkg);
      }
    }

    g_free (p_name);
    g_free (p_version);
    g_free (p_location);
  }

}

static void fillin_pkg_details(pkg_info_t *pkg)
{
  extern GtkWidget *gslapt;
  extern rc_config *global_config;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;
  gchar *short_desc;
  GtkTextBuffer *pkg_full_desc;
  pkg_info_t *latest_pkg = get_newest_pkg(all,pkg->name);
  pkg_info_t *installed_pkg = get_newest_pkg(installed,pkg->name);

  /* set package details */
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_name")),pkg->name);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_location")),pkg->location);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_version")),pkg->version);
  short_desc = gen_short_pkg_description(pkg);
  if (short_desc != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_description")),short_desc);
    free(short_desc);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_description")),"");
  }
  /* dependency information tab */
  if ( pkg->required != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_required")),pkg->required);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_required")),"");
  }
  if ( pkg->conflicts != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_conflicts")),pkg->conflicts);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_conflicts")),"");
  }
  if ( pkg->suggests != NULL ) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_suggests")),pkg->suggests);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_suggests")),"");
  }
  /* description tab */
  pkg_full_desc = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(gslapt,"pkg_description_textview")));
  gtk_text_buffer_set_text(pkg_full_desc,pkg->description,-1);


  /* set status */
  if ((trans->exclude_pkgs->pkg_count > 0 &&
  get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) != NULL) ||
  is_excluded(global_config,pkg) == 1) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("Excluded"));
  } else if (trans->remove_pkgs->pkg_count > 0 &&
  get_exact_pkg(trans->remove_pkgs,pkg->name,pkg->version) != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Removed"));
  } else if (trans->install_pkgs->pkg_count > 0 &&
  get_exact_pkg(trans->install_pkgs,pkg->name,pkg->version) != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Installed"));
  } else if (trans->upgrade_pkgs->pkg_count > 0 && lsearch_upgrade_transaction(trans,pkg) == 1) {
    pkg_info_t *installed_pkg = get_newest_pkg(installed,pkg->name);
    int cmp = cmp_pkg_versions(pkg->version,installed_pkg->version);
    if (cmp == 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Re-Installed"));
    } else if (cmp < 0 ) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Downgraded"));
    } else {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Upgraded"));
    }
  } else if (get_exact_pkg(installed,pkg->name,pkg->version) != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("Installed"));
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("Not Installed"));
  }

  /* set installed info */
  if (installed_pkg != NULL) {
    gchar size_u[20];
    sprintf(size_u,"%d K",installed_pkg->size_u);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_installed_installed_size")),size_u);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_installed_version")),installed_pkg->version);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_installed_installed_size")),"");
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_installed_version")),"");
  }

  /* set latest available info */
  if (latest_pkg != NULL) {
    gchar latest_size_c[20],latest_size_u[20];
    sprintf(latest_size_c,"%d K",latest_pkg->size_c);
    sprintf(latest_size_u,"%d K",latest_pkg->size_u);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_version")),latest_pkg->version);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_source")),latest_pkg->mirror);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_size")),latest_size_c);
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_installed_size")),latest_size_u);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_version")),"");
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_source")),"");
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_size")),"");
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_available_installed_size")),"");
  }

}

static void clear_treeview(GtkTreeView *treeview)
{
  GtkListStore *store;

  store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));
  gtk_list_store_clear(store);
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

  progress_window = create_dl_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),(gchar *)_("Progress"));
  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  lock_toolbar_buttons();
  context_id = gslapt_set_status((gchar *)_("Checking for new package data..."));
  gtk_widget_show(progress_window);
  gdk_threads_leave();

  /* open tmp pkg list file */
  pkg_list_fh_tmp = tmpfile();
  if ( pkg_list_fh_tmp == NULL ) {

    if ( errno ) perror("tmpfile");

    exit(1);
  }

  dl_files = (global_config->sources->count * 3.0 );

  if (_cancelled == 1) {
    _cancelled = 0;
    fclose(pkg_list_fh_tmp);
    gdk_threads_enter();
    gslapt_clear_status(context_id);
    gtk_widget_destroy(progress_window);
    unlock_toolbar_buttons();
    gdk_threads_leave();
    return;
  }

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

    if (_cancelled == 1) {
      _cancelled = 0;
      fclose(pkg_list_fh_tmp);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

    gdk_threads_enter();
    gtk_label_set_text(progress_action_label,global_config->sources->url[i]);
    gtk_label_set_text(progress_message_label,PKG_LIST);
    gdk_threads_leave();

    /* download our PKG_LIST */
    pkg_filename = gen_filename_from_url(global_config->sources->url[i],PKG_LIST);
    pkg_head = head_mirror_data(global_config->sources->url[i],PKG_LIST);
    pkg_local_head = read_head_cache(pkg_filename);

    if (_cancelled == 1) {
      _cancelled = 0;
      fclose(pkg_list_fh_tmp);
      free(pkg_head);
      free(pkg_local_head);
      free(pkg_filename);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

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
        if (_cancelled == 1) {
          unlock_toolbar_buttons();
          _cancelled = 0;
        } else {
          notify((gchar *)_("Source download failed"),global_config->sources->url[i]);
        }
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
      notify((gchar *)_("Source download failed"),global_config->sources->url[i]);
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

    if (_cancelled == 1) {
      _cancelled = 0;
      fclose(pkg_list_fh_tmp);
      free(patch_head);
      free(patch_local_head);
      free(patch_filename);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

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
        if (_cancelled == 1) {
          _cancelled = 0;
          fclose(pkg_list_fh_tmp);
          free(patch_head);
          free(patch_local_head);
          free(patch_filename);
          fclose(tmp_patch_f);
          gdk_threads_enter();
          gslapt_clear_status(context_id);
          gtk_widget_destroy(progress_window);
          unlock_toolbar_buttons();
          gdk_threads_leave();
          return;
        }

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

    if (_cancelled == 1) {
      _cancelled = 0;
      fclose(pkg_list_fh_tmp);
      free(checksum_head);
      free(checksum_local_head);
      free(checksum_filename);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

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
        if (_cancelled == 1) {
          unlock_toolbar_buttons();
          _cancelled = 0;
        } else {
          notify((gchar *)_("Source download failed"),global_config->sources->url[i]);
        }
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
      notify((gchar *)_("Source download failed"),global_config->sources->url[i]);
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

  if (_cancelled == 1) {
    return -1;
  }

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
  GdkCursor *c = gdk_cursor_new(GDK_WATCH);

  gdk_window_set_cursor(gslapt->window,c);
  gdk_flush();
  gdk_cursor_destroy(c);

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
  extern GtkWidget *gslapt;
  extern transaction_t *trans;
  extern struct pkg_list *installed;
  struct pkg_list *installed_ptr;
  GdkCursor *c;

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
      if (_cancelled == 1) {
        _cancelled = 0;
        rebuild_treeviews();
      } else {
        notify((gchar *)_("Error"),(gchar *)_("Package(s) failed to download"));
      }
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
    rebuild_treeviews();
    unlock_toolbar_buttons();
    reset_pkg_view_status();
    clear_execute_active();
    gdk_threads_leave();
    return;
  }

  if (_cancelled == 1) {
    _cancelled = 0;
    gdk_threads_enter();
    unlock_toolbar_buttons();
    reset_pkg_view_status();
    clear_execute_active();
    gdk_threads_leave();
    return;
  }

  /* begin removing, installing, and upgrading */
  if ( install_packages() == FALSE ) {
    gdk_threads_enter();
    notify((gchar *)_("Error"),(gchar *)_("pkgtools returned an error"));
    unlock_toolbar_buttons();
    gdk_threads_leave();
    return;
  }

  /* set busy cursor */
  c = gdk_cursor_new(GDK_WATCH);
  gdk_threads_enter();
  clear_execute_active();
  gdk_window_set_cursor(gslapt->window,c);
  gdk_flush();
  gdk_threads_leave();
  gdk_cursor_destroy(c);

  free_transaction(trans);
  init_transaction(trans);
  /* rebuild the installed list */
  installed_ptr = installed;
  installed = get_installed_pkgs();
  free_pkg_list(installed_ptr);

  gdk_threads_enter();
  /* reset cursor */
  gdk_window_set_cursor(gslapt->window,NULL);
  gdk_flush();
  reset_pkg_view_status();
  unlock_toolbar_buttons();
  notify((gchar *)_("Completed actions"),(gchar *)_("Successfully executed all actions."));
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
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Source"), renderer,
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
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Expression"), renderer,
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
                                strlen((gchar *)_(": Depends: ")) +
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
                                strlen((gchar *)_(", which is required by ")) +
                                strlen(trans->conflict_err->errs[i]->pkg) +
                                strlen((gchar *)_(", is excluded")) + 1;
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
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Package"), renderer,
    "text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(summary_treeview), column);

  snprintf(buf,512,(gchar *)_("%d upgraded, %d newly installed, %d to remove and %d not upgraded."),
    trans->upgrade_pkgs->pkg_count,
    trans->install_pkgs->pkg_count,
    trans->remove_pkgs->pkg_count,
    trans->exclude_pkgs->pkg_count
  );
  gtk_label_set_text(GTK_LABEL(sum_pkg_num),buf);

  /* if we don't have enough free space */
  if ( disk_space(global_config,dl_size - already_dl_size + free_space) != 0 ) {
    notify((gchar *)_("Error"),(gchar *)_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
    return -1;
  }

  if ( already_dl_size > 0 ) {
    int need_to_dl = dl_size - already_dl_size;
    snprintf(buf,512,(gchar *)_("Need to get %0.1d%s/%0.1d%s of archives.\n"),
      (need_to_dl > 1024 ) ? need_to_dl / 1024
        : need_to_dl,
      (need_to_dl > 1024 ) ? "MB" : "kB",
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,(gchar *)_("Need to get %0.1d%s of archives."),
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }
  gtk_label_set_text(GTK_LABEL(sum_dl_size),buf);

  if ( free_space < 0 ) {
    free_space *= -1;
    snprintf(buf,512,(gchar *)_("After unpacking %0.1d%s disk space will be freed."),
      (free_space > 1024 ) ? free_space / 1024
        : free_space,
      (free_space > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,(gchar *)_("After unpacking %0.1d%s of additional disk space will be used."),
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

  gtk_entry_set_text(GTK_ENTRY(w),"");
  rebuild_treeviews();

}

static void mark_upgrade_packages(void)
{
  extern rc_config *global_config;
  extern struct pkg_list *all;
  extern struct pkg_list *installed;
  extern transaction_t *trans;
  extern GtkWidget *gslapt;
  GtkTreeView *treeview;
  GtkTreeModel *model;
  GtkTreeIter iter;
  guint i;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  model = gtk_tree_view_get_model(treeview);

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
            (ladd_deps_to_trans(global_config,trans,all,installed,update_pkg) == 0)
            && ( is_conflicted(trans,all,installed,update_pkg) == NULL )
          ) {
            add_upgrade_to_transaction(trans,installed->pkgs[i],update_pkg);
            if (set_iter_to_pkg(model,&iter,update_pkg)) {
              gchar *ustatus = g_strdup_printf("u%s",update_pkg->name);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
              gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,ustatus,-1);
              g_free(ustatus);
            }
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

  progress_window = create_dl_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),(gchar *)_("Progress"));

  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  gtk_widget_show(progress_window);
  context_id = gslapt_set_status((gchar *)_("Downloading packages..."));
  gdk_threads_leave();

  if (_cancelled == 1) {
    gdk_threads_enter();
    gtk_widget_destroy(progress_window);
    gslapt_clear_status(context_id);
    gdk_threads_leave();
    return FALSE;
  }

  for (i = 0; i < trans->install_pkgs->pkg_count;++i) {

    guint msg_len = strlen(trans->install_pkgs->pkgs[i]->name)
        + strlen("-") + strlen(trans->install_pkgs->pkgs[i]->version)
        + strlen(".") + strlen(".tgz");
    gchar *msg = slapt_malloc(msg_len * sizeof *msg);
    gchar dl_size[20];

    snprintf(msg,
      strlen(trans->install_pkgs->pkgs[i]->name)
      + strlen("-")
      + strlen(trans->install_pkgs->pkgs[i]->version)
      + strlen(".") + strlen(".tgz"),
      "%s-%s.tgz",
      trans->install_pkgs->pkgs[i]->name,
      trans->install_pkgs->pkgs[i]->version
    );
    sprintf(dl_size,"%d K",trans->install_pkgs->pkgs[i]->size_c);

    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->install_pkgs->pkgs[i]->mirror);
    gtk_label_set_text(progress_action_label,(gchar *)_("Downloading..."));
    gtk_label_set_text(progress_message_label,msg);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/pkgs_to_dl)/100);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress")),0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress")),dl_size);
    gdk_threads_leave();

    free(msg);

    if (_cancelled == 1) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return FALSE;
    }

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
    gchar dl_size[20];

    snprintf(msg,
      strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->name)
      + strlen("-")
      + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->version)
      + strlen(".") + strlen(".tgz"),
      "%s-%s.tgz",
      trans->upgrade_pkgs->pkgs[i]->upgrade->name,
      trans->upgrade_pkgs->pkgs[i]->upgrade->version
    );
    sprintf(dl_size,"%d K",trans->upgrade_pkgs->pkgs[i]->upgrade->size_c);

    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->upgrade_pkgs->pkgs[i]->upgrade->mirror);
    gtk_label_set_text(progress_action_label,(gchar *)_("Downloading..."));
    gtk_label_set_text(progress_message_label,msg);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/pkgs_to_dl)/100);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress")),0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress")),dl_size);
    gdk_threads_leave();

    free(msg);

    if (_cancelled == 1) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return FALSE;
    }

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

  progress_window = create_pkgtools_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),(gchar *)_("Progress"));

  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
  progress_pkg_desc = GTK_LABEL(lookup_widget(progress_window,"progress_package_description"));

  gdk_threads_enter();
  gtk_widget_show(progress_window);
  context_id = gslapt_set_status((gchar *)_("Removing packages..."));
  gdk_threads_leave();

  for (i = 0; i < trans->remove_pkgs->pkg_count;++i) {
    gdk_threads_enter();
    gtk_label_set_text(progress_pkg_desc,trans->remove_pkgs->pkgs[i]->description);
    gtk_label_set_text(progress_action_label,(gchar *)_("Uninstalling..."));
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
  context_id = gslapt_set_status((gchar *)_("Installing packages..."));
  gdk_threads_leave();

  /* now for the installs and upgrades */
  count = 0.0;
  for (i = 0;i < trans->queue->count; ++i) {
    if ( trans->queue->pkgs[i]->type == INSTALL ) {
      gdk_threads_enter();
      gtk_label_set_text(progress_pkg_desc,trans->queue->pkgs[i]->pkg.i->description);
      gtk_label_set_text(progress_action_label,(gchar *)_("Installing..."));
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
      gtk_label_set_text(progress_action_label,(gchar *)_("Upgrading..."));
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
  GList *columns;
  guint i;

  if ( new_source == NULL || strlen(new_source) < 1 ) return;

  add_source(global_config->sources,new_source);

  gtk_entry_set_text(new_source_entry,"");

  clear_treeview(source_tree);
  columns = gtk_tree_view_get_columns(source_tree);
  for (i = 0; i < g_list_length(columns); i++ ) {
    GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
    if ( column != NULL ) {
      gtk_tree_view_remove_column(source_tree,column);
    }
  }
  g_list_free(columns);

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
    GList *columns;

    gtk_tree_model_get(model,&iter,0,&source, -1 );

    clear_treeview(source_tree);
    columns = gtk_tree_view_get_columns(source_tree);
    for (i = 0; i < g_list_length(columns); i++ ) {
      GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
      if ( column != NULL ) {
        gtk_tree_view_remove_column(source_tree,column);
      }
    }
    g_list_free(columns);

    i = 0;
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
    notify((gchar *)_("Error"),(gchar *)_("Failed to commit preferences"));
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
  GList *columns;
  guint i;

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
  columns = gtk_tree_view_get_columns(exclude_tree);
  for (i = 0; i < g_list_length(columns); i++ ) {
    GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
    if ( column != NULL ) {
      gtk_tree_view_remove_column(exclude_tree,column);
    }
  }
  g_list_free(columns);

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
    GList *columns;

    gtk_tree_model_get(model,&iter,0,&exclude, -1 );

    clear_treeview(exclude_tree);
    columns = gtk_tree_view_get_columns(exclude_tree);
    for (i = 0; i < g_list_length(columns); i++ ) {
      GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
      if ( column != NULL ) {
        gtk_tree_view_remove_column(exclude_tree,column);
      }
    }
    g_list_free(columns);

    i = 0;
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
    pending_trans_context_id = gslapt_set_status((gchar *)_("Pending changes. Click execute when ready."));
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

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"), FALSE);

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

static gboolean pkg_action_popup_menu(GtkTreeView *treeview, gpointer data)
{
  GtkMenu *menu;
  GdkEventButton *event = (GdkEventButton *)gtk_get_current_event();
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
    return FALSE;

  if ( !gtk_tree_view_get_path_at_pos(treeview,event->x,event->y,&path,&column,NULL,NULL) )
    return FALSE;

  if (event->button != 3 && (event->button == 1 && strcmp(column->title,(gchar *)_("Status")) != 0))
    return FALSE;

  gtk_tree_path_free(path);

  menu = GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(lookup_widget(gslapt,"package1"))));

  gtk_menu_popup(
    menu,
   NULL,
   NULL,
   NULL,
   NULL,
   event->button,
   gtk_get_current_event_time()
  );

  return TRUE;
}

void unmark_package(GtkWidget *gslapt, gpointer user_data) 
{
  GtkTreeView *treeview;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  pkg_info_t *pkg = NULL;
  extern transaction_t *trans;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  guint is_installed = 0,i;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    gchar *status = NULL;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);
    gtk_tree_model_get (model, &iter, 3, &pkg_location, -1);

    if (pkg_name == NULL || pkg_version == NULL || pkg_location == NULL) {
      fprintf(stderr,"failed to get package name and version from selection\n");

      if (pkg_name != NULL)
        g_free(pkg_name);

      if (pkg_version != NULL)
        g_free(pkg_version);

      if (pkg_location != NULL)
        g_free(pkg_location);

      return;
    }

    if (((pkg = get_pkg_by_details(all,pkg_name,pkg_version,pkg_location)) == NULL)) {
      pkg = get_exact_pkg(installed,pkg_name,pkg_version);
      is_installed = 1;
    } else {
      if (get_exact_pkg(installed,pkg_name,pkg_version) != NULL) {
        is_installed = 1;
      }
    }
    if (pkg == NULL) {
      fprintf(stderr,"Failed to find package: %s-%s@%s\n",pkg_name,pkg_version,pkg_location);
      g_free(pkg_name);
      g_free(pkg_version);
      g_free(pkg_location);
      return;
    }
    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

    if (is_installed == 1) {
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_installed.png"),-1);
      status = g_strdup_printf("a%s",pkg->name);
    } else {
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_available.png"),-1);
      status = g_strdup_printf("z%s",pkg->name);
    }
    gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
    g_free(status);

    /* clear the installed version as well if this was an upgrade */
    for (i = 0; i < trans->upgrade_pkgs->pkg_count; ++i) {
      if (strcmp(trans->upgrade_pkgs->pkgs[i]->installed->name,pkg->name) == 0) {
        pkg_info_t *avail_pkg = get_exact_pkg(all,
          trans->upgrade_pkgs->pkgs[i]->installed->name,
          trans->upgrade_pkgs->pkgs[i]->installed->version
        );
        if (avail_pkg == NULL) {
          continue;
        }
        if (set_iter_to_pkg(model,&iter,avail_pkg)) {
          gchar *istatus = g_strdup_printf("i%s",avail_pkg->name);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_installed.png"),-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,istatus,-1);
          g_free(istatus);
        }
      }
    }

    trans = remove_from_transaction(trans,pkg);
    if (trans->install_pkgs->pkg_count == 0 &&
        trans->remove_pkgs->pkg_count == 0 &&
        trans->upgrade_pkgs->pkg_count == 0
    ) {
      clear_execute_active();
    }

  }

  rebuild_package_action_menu();
}

/* parse the dependencies for a package, and add them to the transaction as */
/* needed check to see if a package is conflicted */
int ladd_deps_to_trans(const rc_config *global_config, transaction_t *tran,
                      struct pkg_list *avail_pkgs,
                      struct pkg_list *installed_pkgs, pkg_info_t *pkg)
{
  unsigned int c;
  int dep_return = -1;
  struct pkg_list *deps = NULL;
  extern GtkWidget *gslapt;
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  GtkTreeModel *model = gtk_tree_view_get_model(treeview);
  GtkTreeIter iter;

  if ( global_config->disable_dep_check == TRUE ) return 0;
  if ( pkg == NULL ) return 0;

  deps = init_pkg_list();

  dep_return = get_pkg_dependencies(
    global_config,avail_pkgs,installed_pkgs,pkg,
    deps,tran->conflict_err,tran->missing_err
  );

  /* check to see if there where issues with dep checking */
  /* exclude the package if dep check barfed */
  if ( (dep_return == -1) && (global_config->ignore_dep == FALSE) &&
      (get_exact_pkg(tran->exclude_pkgs,pkg->name,pkg->version) == NULL)
  ) {
    add_exclude_to_transaction(tran,pkg);
    free_pkg_list(deps);
    return -1;
  }

  /* loop through the deps */
  for (c = 0; c < deps->pkg_count;c++) {
    pkg_info_t *dep_installed;
    pkg_info_t *conflicted_pkg = NULL;

    /*
     * the dep wouldn't get this far if it where excluded,
     * so we don't check for that here
     */

    conflicted_pkg = is_conflicted(tran,avail_pkgs,installed_pkgs,deps->pkgs[c]);
    if ( conflicted_pkg != NULL ) {
      add_remove_to_transaction(tran,conflicted_pkg);
      if (set_iter_to_pkg(model,&iter,conflicted_pkg)) {
        gchar *status = g_strdup_printf("r%s",conflicted_pkg->name);
        gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_remove.png"),-1);
        gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
        g_free(status);
      }
    }

    dep_installed = get_newest_pkg(installed_pkgs,deps->pkgs[c]->name);
    if ( dep_installed == NULL ) {
      add_install_to_transaction(tran,deps->pkgs[c]);
      if (set_iter_to_pkg(model,&iter,deps->pkgs[c])) {
        gchar *status = g_strdup_printf("i%s",deps->pkgs[c]->name);
        gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_install.png"),-1);
        gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
        g_free(status);
      }
    } else {
      /* add only if its a valid upgrade */
      if (cmp_pkg_versions(dep_installed->version,deps->pkgs[c]->version) < 0 )
        add_upgrade_to_transaction(tran,dep_installed,deps->pkgs[c]);
        if (set_iter_to_pkg(model,&iter,deps->pkgs[c])) {
          gchar *status = g_strdup_printf("u%s",deps->pkgs[c]->name);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_upgrade.png"),-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
          g_free(status);
        }
    }

  }

  free_pkg_list(deps);

  return 0;
}

static int set_iter_to_pkg(GtkTreeModel *model, GtkTreeIter *iter,
                           pkg_info_t *pkg)
{
  gboolean valid;

  valid = gtk_tree_model_get_iter_first(model,iter);
  while (valid) {
    gchar *name,*version,*location;
    gtk_tree_model_get(model,iter,
      NAME_COLUMN,&name,
      VERSION_COLUMN,&version,
      LOCATION_COLUMN,&location,
      -1
    );

    if (name == NULL || version == NULL || location == NULL) {
      valid = gtk_tree_model_iter_next(model,iter);
      continue;
    }

    if (strcmp(name,pkg->name) == 0 &&
        strcmp(version,pkg->version) == 0 &&
        strcmp(location,pkg->location) == 0) {
      g_free(name);
      g_free(version);
      g_free(location);
      return 1;
    }
    g_free(name);
    g_free(version);
    g_free(location);

    valid = gtk_tree_model_iter_next(model,iter);
  }
  return 0;
}

static void reset_pkg_view_status(void)
{
  extern struct pkg_list *installed;
  extern GtkWidget *gslapt;
  gboolean valid;
  GtkTreeIter iter;
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  GtkTreeModel *model = gtk_tree_view_get_model(treeview);

  valid = gtk_tree_model_get_iter_first(model,&iter);
  while (valid) {
    gchar *name = NULL,*version = NULL, *status = NULL;
    gtk_tree_model_get(model,&iter,
      NAME_COLUMN,&name,
      VERSION_COLUMN,&version,
      -1
    );
    if (name == NULL || version == NULL) {

      if (name != NULL)
        g_free(name);

      if (version != NULL)
        g_free(version);

      valid = gtk_tree_model_iter_next(model,&iter);
      continue;
    }

    if (get_exact_pkg(installed,name,version) == NULL) {
      status = g_strdup_printf("z%s",name);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_available.png"),-1);
    } else {
      status = g_strdup_printf("a%s",name);
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_ICON_COLUMN,create_pixbuf("pkg_action_installed.png"),-1);
    }
    gtk_list_store_set(GTK_LIST_STORE(model),&iter,STATUS_COLUMN,status,-1);
    g_free(status);

    g_free(name);
    g_free(version);

    valid = gtk_tree_model_iter_next(model,&iter);
  }
}

void build_treeview_columns(GtkWidget *treeview)
{
  GtkTreeSelection *select;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;

  /* column for installed status */
  renderer = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Status"), renderer,
    "pixbuf", STATUS_ICON_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, STATUS_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for name */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Name"), renderer,
    "text", NAME_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, NAME_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for version */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Version"), renderer,
    "text", VERSION_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, VERSION_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for location */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Location"), renderer,
    "text", LOCATION_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, LOCATION_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for short description */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Description"), renderer,
    "text", DESC_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, DESC_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* invisible column to sort installed by */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Installed"), renderer,
    "text", STATUS_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
    G_CALLBACK (show_pkg_details), NULL);

  g_signal_connect(G_OBJECT(treeview),"cursor-changed",
    G_CALLBACK(pkg_action_popup_menu),NULL);

}

static int lsearch_upgrade_transaction(transaction_t *tran,pkg_info_t *pkg)
{
  unsigned int i,found = 1, not_found = 0;
  for (i = 0; i < tran->upgrade_pkgs->pkg_count;i++) {
    if (strcmp(pkg->name,tran->upgrade_pkgs->pkgs[i]->upgrade->name) == 0 &&
    strcmp(pkg->version,tran->upgrade_pkgs->pkgs[i]->upgrade->version) == 0 &&
    strcmp(pkg->location,tran->upgrade_pkgs->pkgs[i]->upgrade->location) == 0) {
      return found;
    }
  }
  return not_found;
}

void open_icon_legend (GtkObject *object, gpointer user_data)
{
  GtkWidget *icon_legend = create_icon_legend();
  gtk_widget_show(icon_legend);
}


void on_button_cancel_clicked(GtkButton *button, gpointer user_data)
{
  _cancelled = 1;
}

static void build_package_action_menu(pkg_info_t *pkg)
{
  GtkMenu *menu;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;
  extern transaction_t *trans;
  extern rc_config *global_config;
  extern GtkWidget *gslapt;
  pkg_info_t *newest_installed = NULL, *upgrade_pkg = NULL;
  guint is_installed = 0,is_newest = 1,is_exclude = 0,is_downloadable = 0,is_downgrade = 0;

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

  menu = GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(lookup_widget(gslapt,"package1"))));

  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"upgrade1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"re_install1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"re_install1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"downgrade1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"install1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"remove1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"unmark1"),FALSE);

  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"upgrade1")),add_pkg_for_install,GTK_OBJECT(gslapt));
  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"re_install1")),add_pkg_for_reinstall,GTK_OBJECT(gslapt));
  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"downgrade1")),add_pkg_for_reinstall,GTK_OBJECT(gslapt));
  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"install1")),add_pkg_for_install,GTK_OBJECT(gslapt));
  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"remove1")),add_pkg_for_removal,GTK_OBJECT(gslapt));
  g_signal_handlers_disconnect_by_func(GTK_OBJECT(lookup_widget(GTK_WIDGET(menu),"unmark1")),unmark_package,GTK_OBJECT(gslapt));

  g_signal_connect_swapped(G_OBJECT(lookup_widget(GTK_WIDGET(menu),"upgrade1")),
    "activate", G_CALLBACK (add_pkg_for_install), GTK_WIDGET(gslapt));
  g_signal_connect_swapped((gpointer)lookup_widget(GTK_WIDGET(menu),"re_install1"),
    "activate", G_CALLBACK(add_pkg_for_reinstall),GTK_OBJECT(gslapt));
  g_signal_connect_swapped((gpointer)lookup_widget(GTK_WIDGET(menu),"downgrade1"),
    "activate", G_CALLBACK(add_pkg_for_reinstall),GTK_OBJECT(gslapt));
  g_signal_connect_swapped(G_OBJECT(lookup_widget(GTK_WIDGET(menu),"install1")),
    "activate", G_CALLBACK (add_pkg_for_install), GTK_WIDGET(gslapt));
  g_signal_connect_swapped(G_OBJECT(lookup_widget(GTK_WIDGET(menu),"remove1")),
    "activate",G_CALLBACK (add_pkg_for_removal), GTK_WIDGET(gslapt));
  g_signal_connect_swapped(G_OBJECT(lookup_widget(GTK_WIDGET(menu),"unmark1")),
    "activate",G_CALLBACK (unmark_package), GTK_WIDGET(gslapt));

  if ( search_transaction(trans,pkg->name) == 0 ) {
    if ( is_exclude == 0 ) {
      /* upgrade */
      if ( is_installed == 1 && is_newest == 0 && (search_transaction_by_pkg(trans,upgrade_pkg) == 0 ) ) {
        gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"upgrade1"),TRUE);
      /* re-install */
      }else if ( is_installed == 1 && is_newest == 1 && is_downloadable == 1 ) {
        gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"re_install1"),TRUE);
      /* this is for downgrades */
      }else if ( is_installed == 0 && is_downgrade == 1 && is_downloadable == 1 ) {
        gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"downgrade1"),TRUE);
      /* straight up install */
      }else if ( is_installed == 0 && is_downloadable == 1 ) {
        gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"install1"),TRUE);
      }
    }

  }

  if (
    is_installed == 1 && is_exclude != 1 &&
    (search_transaction(trans,pkg->name) == 0 )
  ) {
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"remove1"),TRUE);
  }

  if ((get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) == NULL) && 
    (search_transaction_by_pkg(trans,pkg) == 1)
  ) {
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"unmark1"),TRUE);
  }

}

static void rebuild_package_action_menu(void)
{
  GtkTreeView *treeview;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  extern GtkWidget *gslapt;
  extern struct pkg_list *installed;
  extern struct pkg_list *all;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    pkg_info_t *pkg = NULL;

    gtk_tree_model_get (model, &iter, 1, &pkg_name, -1);
    gtk_tree_model_get (model, &iter, 2, &pkg_version, -1);
    gtk_tree_model_get (model, &iter, 3, &pkg_location, -1);

    if (pkg_name == NULL || pkg_version == NULL || pkg_location == NULL) {
      if (pkg_name != NULL)
        g_free(pkg_name);

      if (pkg_version != NULL)
        g_free(pkg_version);

      if (pkg_location != NULL)
        g_free(pkg_location);

      return;
    }

    pkg = get_pkg_by_details(all,pkg_name,pkg_version,pkg_location);
    if (pkg == NULL) {
      pkg = get_pkg_by_details(installed,pkg_name,pkg_version,pkg_location);
    }
    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

    if (pkg != NULL) {
      build_package_action_menu(pkg);
    }

  }

}


void on_unmark_all1_activate(GtkMenuItem *menuitem, gpointer user_data)
{
  extern transaction_t *trans;
  GdkCursor *c;

  c = gdk_cursor_new(GDK_WATCH);
  gdk_flush();

  lock_toolbar_buttons();

  /* reset our currently selected packages */
  free_transaction(trans);
  init_transaction(trans);

  /* rebuild_treeviews(); */
  reset_pkg_view_status();
  unlock_toolbar_buttons();
  clear_execute_active();

  gdk_cursor_destroy(c);
}

