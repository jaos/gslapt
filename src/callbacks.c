/*
 * Copyright (C) 2003-2011 Jason Woodward <woodwardj at jaos dot org>
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
#include "settings.h"
#include "series.h"

extern GtkWidget *gslapt;
extern GslaptSettings *gslapt_settings;
extern GHashTable *gslapt_series_map;
extern slapt_rc_config *global_config;
extern slapt_pkg_list_t *all;
extern slapt_pkg_list_t *installed;
extern slapt_transaction_t *trans;
extern char rc_location[];


static GtkWidget *progress_window;
static GtkWidget *preferences_window;
G_LOCK_DEFINE_STATIC (_cancelled);
static volatile guint _cancelled = 0;
static gboolean sources_modified = FALSE;
static gboolean excludes_modified = FALSE;
static volatile guint pending_trans_context_id = 0;

static gboolean pkg_action_popup_menu (GtkTreeView *treeview, gpointer data);
static int set_iter_to_pkg (GtkTreeModel *model, GtkTreeIter *iter,
                            slapt_pkg_info_t *pkg);
static slapt_pkg_upgrade_t *lsearch_upgrade_transaction (slapt_transaction_t *tran,slapt_pkg_info_t *pkg);
static void build_package_action_menu (slapt_pkg_info_t *pkg);
static void rebuild_package_action_menu (void);
static void mark_upgrade_packages (void);
static void fillin_pkg_details (slapt_pkg_info_t *pkg);
static void get_package_data (void);
static void rebuild_treeviews (GtkWidget *current_window,gboolean reload);
static guint gslapt_set_status (const gchar *);
static void gslapt_clear_status (guint context_id);
static void lock_toolbar_buttons (void);
static void unlock_toolbar_buttons (void);
static void build_sources_treeviewlist (GtkWidget *treeview);
#ifdef SLAPT_HAS_GPGME
static void build_verification_sources_treeviewlist (GtkWidget *treeview);
#endif
static void build_exclude_treeviewlist (GtkWidget *treeview);
static int populate_transaction_window (GtkWidget *trans_window);
char *download_packages (void);
static gboolean install_packages (void);
static void set_execute_active (void);
static void clear_execute_active (void);
static void notify (const char *title,const char *message);
static void reset_search_list (void);
static int ladd_deps_to_trans (slapt_transaction_t *tran, slapt_pkg_list_t *avail_pkgs,
                               slapt_pkg_list_t *installed_pkgs, slapt_pkg_info_t *pkg);
static gboolean toggle_source_status (GtkTreeView *treeview, gpointer data);
static void display_dep_error_dialog (slapt_pkg_info_t *pkg,guint m, guint c);
static void exclude_dep_error_callback (GtkObject *object, gpointer user_data);
static void install_dep_error_callback (GtkObject *object, gpointer user_data);
static void view_installed_or_available_packages (gboolean show_installed, gboolean show_available);

static int set_iter_for_install(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg);
static int set_iter_for_reinstall(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg);
static int set_iter_for_downgrade(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg);
static int set_iter_for_upgrade(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg);
static int set_iter_for_remove(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg);

static void set_busy_cursor (void);
static void unset_busy_cursor (void);
static SLAPT_PRIORITY_T convert_gslapt_priority_to_slapt_priority(gint p);
static gint convert_slapt_priority_to_gslapt_priority(SLAPT_PRIORITY_T p);

gboolean gslapt_window_resized(GtkWindow *window, GdkEvent *event, gpointer data)
{
  const char *widget_name = gtk_widget_get_name(GTK_WIDGET(window));
  gint x, y, width, height;

  x      = event->configure.x;
  y      = event->configure.y;
  width  = event->configure.width;
  height = event->configure.height;

  if (strcmp(widget_name,"gslapt") == 0) {
    gslapt_settings->x      = x;
    gslapt_settings->y      = y;
    gslapt_settings->width  = width;
    gslapt_settings->height = height;
  } else if (strcmp(widget_name,"window_preferences") == 0) {
    gslapt_settings->pref_x      = x;
    gslapt_settings->pref_y      = y;
    gslapt_settings->pref_width  = width;
    gslapt_settings->pref_height = height;
  } else if (strcmp(widget_name,"changelog_window") == 0) {
    gslapt_settings->cl_x      = x;
    gslapt_settings->cl_y      = y;
    gslapt_settings->cl_width  = width;
    gslapt_settings->cl_height = height;
  } else if (strcmp(widget_name,"transaction_window") == 0) {
    gslapt_settings->tran_x      = x;
    gslapt_settings->tran_y      = y;
    gslapt_settings->tran_width  = width;
    gslapt_settings->tran_height = height;
  } else {
    fprintf(stderr, "need to handle widget name: %s\n", widget_name);
  }

  return FALSE;
}

void on_gslapt_destroy (GtkObject *object, gpointer user_data) 
{
  slapt_free_transaction(trans);
  slapt_free_pkg_list(all);
  slapt_free_pkg_list(installed);
  slapt_free_rc_config(global_config);
  gslapt_series_map_free(gslapt_series_map);

  gslapt_write_rc(gslapt_settings);
  gslapt_free_rc(gslapt_settings);

  gtk_main_quit();
  exit(0);
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
  set_busy_cursor();
  mark_upgrade_packages();
  if (trans->install_pkgs->pkg_count > 0 || trans->upgrade_pkgs->pkg_count > 0) {
    set_execute_active();
  }
  unset_busy_cursor();
}

void execute_callback (GtkObject *object, gpointer user_data) 
{
  GtkWidget *trans_window;

  if (
    trans->install_pkgs->pkg_count == 0
    && trans->upgrade_pkgs->pkg_count == 0
    && trans->remove_pkgs->pkg_count == 0
  ) return;

  trans_window = (GtkWidget *)create_transaction_window();

  if ((gslapt_settings->tran_x == gslapt_settings->tran_y == gslapt_settings->tran_width == gslapt_settings->tran_height == 0)) {
    gtk_window_set_default_size(GTK_WINDOW(trans_window),
      gslapt_settings->tran_width, gslapt_settings->tran_height);
    gtk_window_move(GTK_WINDOW(trans_window),
      gslapt_settings->tran_x, gslapt_settings->tran_y);
  }

  if ( populate_transaction_window(trans_window) == 0 ) {
    gtk_widget_show(trans_window);
  } else {
    gtk_widget_destroy(trans_window);
  }
}

void open_preferences (GtkMenuItem *menuitem, gpointer user_data) 
{
  GtkWidget *preferences;
  GtkEntry *working_dir;
  GtkTreeView *source_tree,*exclude_tree;
  #ifdef SLAPT_HAS_GPGME
  GtkTreeView *verification_source_tree;
  #endif

  preferences = (GtkWidget *)create_window_preferences();

  if ((gslapt_settings->pref_x == gslapt_settings->pref_y == gslapt_settings->pref_width == gslapt_settings->pref_height == 0)) {
    gtk_window_set_default_size(GTK_WINDOW(preferences),
      gslapt_settings->pref_width, gslapt_settings->pref_height);
    gtk_window_move(GTK_WINDOW(preferences),
      gslapt_settings->pref_x, gslapt_settings->pref_y);
  }

  working_dir = GTK_ENTRY(lookup_widget(preferences,"preferences_working_dir_entry"));
  gtk_entry_set_text(working_dir,global_config->working_dir);

  source_tree = GTK_TREE_VIEW(lookup_widget(preferences,"preferences_sources_treeview"));
  build_sources_treeviewlist((GtkWidget *)source_tree);

  #ifdef SLAPT_HAS_GPGME
  verification_source_tree = GTK_TREE_VIEW(lookup_widget(preferences,"preferences_verification_sources_treeview"));
  build_verification_sources_treeviewlist((GtkWidget *)verification_source_tree);
  #endif

  exclude_tree = GTK_TREE_VIEW(lookup_widget(preferences,"preferences_exclude_treeview"));
  build_exclude_treeviewlist((GtkWidget *)exclude_tree);

  gtk_widget_show(preferences);
  preferences_window = preferences;
}

void search_activated (GtkWidget *gslapt, gpointer user_data) 
{
  gboolean valid = FALSE, exists = FALSE;
  GtkTreeIter iter;
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));
  GtkEntryCompletion *completion = gtk_entry_get_completion(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));
  GtkTreeModel *completions = gtk_entry_completion_get_model(completion);

  build_searched_treeviewlist(GTK_WIDGET(treeview),pattern);

  /* add search to completion */
  valid = gtk_tree_model_get_iter_first(completions,&iter);
  while (valid) {
    gchar *string = NULL;
    gtk_tree_model_get(completions,&iter,0,&string,-1);
    if (strcmp(string,pattern) == 0)
      exists = TRUE;
    g_free(string);
    valid = gtk_tree_model_iter_next(completions,&iter);
  }
  if (!exists) {
    gtk_list_store_append(GTK_LIST_STORE(completions),&iter);
    gtk_list_store_set(GTK_LIST_STORE(completions),&iter,
      0,pattern,
      -1
    );
  }

}

void add_pkg_for_install (GtkWidget *gslapt, gpointer user_data) 
{
  slapt_pkg_info_t *pkg = NULL,
                   *installed_pkg = NULL;
  GtkTreeView *treeview;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkBin *caller_button = (GtkBin *)user_data;
  GtkLabel *caller_button_label = GTK_LABEL(gtk_bin_get_child(caller_button));
  GtkTreeModel *model;
  GtkTreeIter actual_iter,filter_iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModelSort *package_model;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  if ( gtk_tree_selection_get_selected(selection,(GtkTreeModel **)&package_model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;

    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

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
      pkg = slapt_get_newest_pkg(all,pkg_name);
    } else if ( strcmp(gtk_label_get_text(caller_button_label),(gchar *)_("Re-Install")) == 0) {
      pkg = slapt_get_exact_pkg(all,pkg_name,pkg_version);
    } else {
      pkg = slapt_get_pkg_by_details(all,pkg_name,pkg_version,pkg_location);
    }

    if ( pkg == NULL ) {
      fprintf(stderr,"Failed to find package: %s-%s@%s\n",pkg_name,pkg_version,pkg_location);
      g_free(pkg_name);
      g_free(pkg_version);
      g_free(pkg_location);
      return;
    }

    installed_pkg = slapt_get_newest_pkg(installed,pkg_name);

    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

  }

  if ( pkg == NULL ) {
    fprintf(stderr,"No package to work with\n");
    return;
  }

  /* convert sort model and iter to filter */
  gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model),&filter_iter,&iter);
  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  /* convert filter to regular tree */
  gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model),&actual_iter,&filter_iter);
  model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  /* a=installed,i=install,r=remove,u=upgrade,z=available */

  /* if it is not already installed, install it */
  if ( installed_pkg == NULL ) {
    guint missing_count = trans->missing_err->err_count,
          conflict_count = trans->conflict_err->err_count;

    if ( ladd_deps_to_trans(trans,all,installed,pkg) == 0 ) {
      slapt_pkg_list_t *conflicts = slapt_is_conflicted(trans,all,installed,pkg);

      slapt_add_install_to_transaction(trans,pkg);
      set_iter_for_install(model, &actual_iter, pkg);
      set_execute_active();

      /* if there is a conflict, we schedule the conflict for removal */
      if ( conflicts->pkg_count > 0) {
        unsigned int cindex = 0;
        for (cindex = 0; cindex < conflicts->pkg_count; cindex++) {
          slapt_pkg_info_t *conflicted_pkg = conflicts->pkgs[cindex];
          slapt_add_remove_to_transaction(trans,conflicted_pkg);
          set_execute_active();
          if (set_iter_to_pkg(model,&actual_iter,conflicted_pkg)) {
            set_iter_for_remove(model, &actual_iter, conflicted_pkg);
          }
        }
      }
      slapt_free_pkg_list(conflicts);
    } else {
      display_dep_error_dialog(pkg,missing_count,conflict_count);
    }

  } else { /* else we upgrade or reinstall */
     int ver_cmp;

    /* it is already installed, attempt an upgrade */
    if (
      ((ver_cmp = slapt_cmp_pkgs(installed_pkg,pkg)) < 0) ||
      (global_config->re_install == TRUE)
    ) {
        guint missing_count = trans->missing_err->err_count,
          conflict_count = trans->conflict_err->err_count;

      if ( ladd_deps_to_trans(trans,all,installed,pkg) == 0 ) {
        slapt_pkg_list_t *conflicts = slapt_is_conflicted(trans,all,installed,pkg);

        if (conflicts->pkg_count > 0) {
          unsigned int cindex = 0;
          for (cindex = 0; cindex < conflicts->pkg_count;cindex++) {
            slapt_pkg_info_t *conflicted_pkg = conflicts->pkgs[cindex];
            fprintf(stderr,"%s conflicts with %s\n",pkg->name,conflicted_pkg->name);
            slapt_add_remove_to_transaction(trans,conflicted_pkg);
            set_execute_active();
            if (set_iter_to_pkg(model,&actual_iter,conflicted_pkg)) {
              set_iter_for_remove(model, &actual_iter, conflicted_pkg);
            }
          }
        }else{

          if (global_config->re_install == TRUE)
            slapt_add_reinstall_to_transaction(trans,installed_pkg,pkg);
          else
            slapt_add_upgrade_to_transaction(trans,installed_pkg,pkg);

          if (global_config->re_install == TRUE) {
            if (ver_cmp == 0) {
              set_iter_for_reinstall(model,&actual_iter,pkg);
            } else {
              set_iter_for_downgrade(model,&actual_iter,pkg);
              if (set_iter_to_pkg(model,&actual_iter,installed_pkg)) {
                set_iter_for_downgrade(model,&actual_iter,installed_pkg);
              }
            }
          } else {
            slapt_pkg_info_t *inst_avail = slapt_get_exact_pkg(all,installed_pkg->name,installed_pkg->version);
            set_iter_for_upgrade(model, &actual_iter, pkg);
            if ( pkg != NULL && set_iter_to_pkg(model,&actual_iter,pkg)) {
              set_iter_for_upgrade(model, &actual_iter, pkg);
            }
            if (installed_pkg != NULL && set_iter_to_pkg(model,&actual_iter,installed_pkg)) {
              set_iter_for_upgrade(model, &actual_iter, installed_pkg);
            }
            if (inst_avail != NULL && set_iter_to_pkg(model,&actual_iter,inst_avail)) {
              set_iter_for_upgrade(model, &actual_iter, inst_avail);
            }
          }
          set_execute_active();
        }

        slapt_free_pkg_list(conflicts);
      } else {
        display_dep_error_dialog(pkg,missing_count,conflict_count);
      }

    }

  }

  rebuild_package_action_menu();
}

void add_pkg_for_removal (GtkWidget *gslapt, gpointer user_data) 
{
  GtkTreeView *treeview;
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkTreeModelSort *package_model;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  if ( gtk_tree_selection_get_selected(selection,(GtkTreeModel **)&package_model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    slapt_pkg_info_t *pkg;

    gtk_tree_model_get (GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
    gtk_tree_model_get (GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
    gtk_tree_model_get (GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

    if ( (pkg = slapt_get_exact_pkg(installed,pkg_name,pkg_version)) != NULL ) {
      guint c;
      slapt_pkg_list_t *deps;
      GtkTreeModel *model;
      GtkTreeIter filter_iter,actual_iter;
      GtkTreeModelFilter *filter_model;

      /* convert sort model and iter to filter */
      gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model),&filter_iter,&iter);
      filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
      /* convert filter to regular tree */
      gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model),&actual_iter,&filter_iter);
      model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

      deps = slapt_is_required_by(global_config, all, installed, trans->install_pkgs, trans->remove_pkgs, pkg);

      slapt_add_remove_to_transaction(trans,pkg);
      set_iter_for_remove(model, &actual_iter, pkg);
      set_execute_active();

      for (c = 0; c < deps->pkg_count;c++) {
        slapt_pkg_info_t *dep = deps->pkgs[c];
        /* need to check that it is actually installed */
        slapt_pkg_info_t *installed_dep = slapt_get_exact_pkg(installed, dep->name, dep->version);
        if (installed_dep != NULL ) {
          slapt_add_remove_to_transaction(trans, installed_dep);
          if (set_iter_to_pkg(model, &actual_iter, installed_dep)) {
            set_iter_for_remove(model, &actual_iter, installed_dep);
          }
        }
      }

      slapt_free_pkg_list(deps);

    }

    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

  }

  rebuild_package_action_menu();

}

void build_package_treeviewlist (GtkWidget *treeview)
{
  GtkTreeIter iter;
  guint i = 0;
  GtkTreeModel *base_model;
  GtkTreeModelFilter *filter_model;
  GtkTreeModelSort *package_model;

  base_model = GTK_TREE_MODEL(gtk_list_store_new (
    NUMBER_OF_COLUMNS,
    GDK_TYPE_PIXBUF, /* status icon */
    G_TYPE_STRING, /* name */
    G_TYPE_STRING, /* version */
    G_TYPE_STRING, /* location */
    G_TYPE_STRING, /* series */
    G_TYPE_STRING, /*desc */
    G_TYPE_UINT, /* size */
    G_TYPE_STRING, /* status */
    G_TYPE_BOOLEAN, /* is installed */
    G_TYPE_BOOLEAN, /* visible */
    G_TYPE_BOOLEAN, /* marked */
    G_TYPE_BOOLEAN /* is an upgrade */
  ));

  for (i = 0; i < all->pkg_count; i++ ) {
    /* we use this for sorting the status */
    /* a=installed,i=install,r=remove,u=upgrade,z=available */
    gchar *status = NULL;
    gboolean is_inst = FALSE, is_an_upgrade = FALSE;
    GdkPixbuf *status_icon = NULL;
    gchar *short_desc = slapt_gen_short_pkg_description(all->pkgs[i]);
    slapt_pkg_info_t *installed_pkg = NULL, *newer_available_pkg = NULL;
    gchar *location = NULL, *series = NULL;

    installed_pkg = slapt_get_newest_pkg(installed,all->pkgs[i]->name);
    if (installed_pkg != NULL) {
      int cmp = slapt_cmp_pkgs(all->pkgs[i], installed_pkg);
      if (strcmp(all->pkgs[i]->version,installed_pkg->version) == 0) {
        is_inst = TRUE;
      } else if (cmp > 0) {
        is_an_upgrade = TRUE;

        /* we need to see if there is another available package
           that is newer than this one */
        if ( (newer_available_pkg = slapt_get_newest_pkg(all, all->pkgs[i]->name)) != NULL) {
          if ( slapt_cmp_pkgs(all->pkgs[i], newer_available_pkg) < 0 )
            is_an_upgrade = FALSE;
        }

      }
    }

    if (slapt_get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name,all->pkgs[i]->version) != NULL) {
      /* if it's excluded */
      if ((slapt_get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name, all->pkgs[i]->version) != NULL) ||
           slapt_is_excluded(global_config,all->pkgs[i]) == 1) {
        status_icon = create_pixbuf("pkg_action_available_excluded.png");
      } else {
        status_icon = create_pixbuf("pkg_action_available.png");
      }
      status = g_strdup_printf("z%s",all->pkgs[i]->name);
      location = all->pkgs[i]->location;
    } else if (slapt_get_exact_pkg(trans->remove_pkgs,all->pkgs[i]->name, all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_remove.png");
      status = g_strdup_printf("r%s",all->pkgs[i]->name);
      location = all->pkgs[i]->location;
    } else if (slapt_get_exact_pkg(trans->install_pkgs,all->pkgs[i]->name, all->pkgs[i]->version) != NULL) {
      status_icon = create_pixbuf("pkg_action_install.png");
      status = g_strdup_printf("i%s",all->pkgs[i]->name);
      location = all->pkgs[i]->location;
    } else if (lsearch_upgrade_transaction(trans,all->pkgs[i]) != NULL) {
      status_icon = create_pixbuf("pkg_action_upgrade.png");
      status = g_strdup_printf("u%s",all->pkgs[i]->name);
      location = all->pkgs[i]->location;
    } else if (is_inst) {
      /* if it's excluded */
      if ((slapt_get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name, all->pkgs[i]->version) != NULL) ||
           slapt_is_excluded(global_config,all->pkgs[i]) == 1) {
        status_icon = create_pixbuf("pkg_action_installed_excluded.png");
      } else {
        status_icon = create_pixbuf("pkg_action_installed.png");
      }
      status = g_strdup_printf("a%s",all->pkgs[i]->name);
      location = installed_pkg->location;
    } else {
      /* if it's excluded */
      if ((slapt_get_exact_pkg(trans->exclude_pkgs,all->pkgs[i]->name, all->pkgs[i]->version) != NULL) ||
           slapt_is_excluded(global_config,all->pkgs[i]) == 1) {
        status_icon = create_pixbuf("pkg_action_available_excluded.png");
      } else {
        status_icon = create_pixbuf("pkg_action_available.png");
      }
      status = g_strdup_printf("z%s",all->pkgs[i]->name);
      location = all->pkgs[i]->location;
    }

    series = gslapt_series_map_lookup(gslapt_series_map, location);
    if (series == NULL)
      series = g_strdup(location);

    gtk_list_store_append(GTK_LIST_STORE(base_model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(base_model), &iter,
      STATUS_ICON_COLUMN,status_icon,
      NAME_COLUMN,all->pkgs[i]->name,
      VERSION_COLUMN,all->pkgs[i]->version,
      LOCATION_COLUMN,location,
      SERIES_COLUMN,series,
      DESC_COLUMN,short_desc,
      SIZE_COLUMN,all->pkgs[i]->size_u,
      STATUS_COLUMN,status,
      INST_COLUMN, is_inst,
      VISIBLE_COLUMN,TRUE,
      MARKED_COLUMN, FALSE,
      UPGRADEABLE_COLUMN, is_an_upgrade,
      -1
    );

    gdk_pixbuf_unref(status_icon);
    g_free(status);
    g_free(short_desc);
    g_free(series);
  }

  for (i = 0; i < installed->pkg_count; ++i) {
    /* do not duplicate those packages that are still available from the package sources */
    if (slapt_get_exact_pkg(all,installed->pkgs[i]->name,installed->pkgs[i]->version) == NULL) {
      /* we use this for sorting the status */
      /* a=installed,i=install,r=remove,u=upgrade,z=available */
      gchar *status = NULL;
      GdkPixbuf *status_icon = NULL;
      gchar *short_desc = slapt_gen_short_pkg_description(installed->pkgs[i]);

      if (slapt_get_exact_pkg(trans->remove_pkgs,installed->pkgs[i]->name, installed->pkgs[i]->version) != NULL) {
        status_icon = create_pixbuf("pkg_action_remove.png");
        status = g_strdup_printf("r%s",installed->pkgs[i]->name);
      } else {
        /* if it's excluded */
        if ((slapt_get_exact_pkg(trans->exclude_pkgs,installed->pkgs[i]->name, installed->pkgs[i]->version) != NULL) ||
            slapt_is_excluded(global_config,installed->pkgs[i]) == 1) {
          status_icon = create_pixbuf("pkg_action_installed_excluded.png");
        } else {
          status_icon = create_pixbuf("pkg_action_installed.png");
        }
        status = g_strdup_printf("a%s",installed->pkgs[i]->name);
      }

      gtk_list_store_append (GTK_LIST_STORE(base_model), &iter);
      gtk_list_store_set ( GTK_LIST_STORE(base_model), &iter,
        STATUS_ICON_COLUMN,status_icon,
        NAME_COLUMN,installed->pkgs[i]->name,
        VERSION_COLUMN,installed->pkgs[i]->version,
        LOCATION_COLUMN,installed->pkgs[i]->location,
        SERIES_COLUMN,installed->pkgs[i]->location,
        DESC_COLUMN,short_desc,
        SIZE_COLUMN,installed->pkgs[i]->size_u,
        STATUS_COLUMN,status,
        INST_COLUMN, TRUE,
        VISIBLE_COLUMN,TRUE,
        MARKED_COLUMN, FALSE,
        UPGRADEABLE_COLUMN, FALSE,
        -1
      );

      gdk_pixbuf_unref(status_icon);
      g_free(status);
      g_free(short_desc);
    }
  }

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(base_model,NULL));
  gtk_tree_model_filter_set_visible_column(filter_model,VISIBLE_COLUMN);
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(filter_model)));
  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(package_model));

  if (gslapt->window != NULL) {
    unset_busy_cursor();
  }
}


void build_searched_treeviewlist (GtkWidget *treeview, gchar *pattern)
{
  gboolean valid;
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  slapt_pkg_list_t *a_matches = NULL,*i_matches = NULL;
  GtkTreeModelSort *package_model;
  gboolean view_list_all = FALSE, view_list_installed = FALSE,
           view_list_available = FALSE, view_list_marked = FALSE,
           view_list_upgradeable = FALSE;
  slapt_regex_t *series_regex = NULL;

  if (pattern == NULL || (strcmp(pattern,"") == 0)) {
    reset_search_list();
  } else {
    series_regex = slapt_init_regex(pattern);
  }

  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  view_list_all         = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_all_packages_menu")));
  view_list_available   = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_available_packages_menu")));
  view_list_installed   = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_installed_packages_menu")));
  view_list_marked      = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_marked_packages_menu")));
  view_list_upgradeable = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_upgradeable_packages_menu")));

  a_matches = slapt_search_pkg_list(all,pattern);
  i_matches = slapt_search_pkg_list(installed,pattern);
  valid = gtk_tree_model_get_iter_first(base_model,&iter);
  while (valid) {
    gchar *name = NULL,*version = NULL,*location = NULL, *series = NULL;
    slapt_pkg_info_t *a_pkg = NULL, *i_pkg = NULL;
    gboolean marked       = FALSE;
    gboolean upgradeable  = FALSE;
    gboolean series_match = FALSE;

    gtk_tree_model_get(base_model,&iter,
      NAME_COLUMN, &name,
      VERSION_COLUMN, &version,
      LOCATION_COLUMN, &location,
      SERIES_COLUMN,&series,
      MARKED_COLUMN, &marked,
      UPGRADEABLE_COLUMN, &upgradeable,
      -1
    );

    if (series_regex != NULL) {
      slapt_execute_regex(series_regex, series);
      if (series_regex->reg_return == 0) {
        series_match = TRUE;
      }
    }
  
    a_pkg = slapt_get_pkg_by_details(a_matches,name,version,location);
    i_pkg = slapt_get_pkg_by_details(i_matches,name,version,location);

    if (view_list_installed && i_pkg != NULL) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if (view_list_available && a_pkg != NULL) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if (view_list_all && (a_pkg != NULL || i_pkg != NULL)) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if (view_list_marked && marked && (a_pkg != NULL || i_pkg != NULL)) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if (view_list_upgradeable && upgradeable && a_pkg != NULL) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if ( series_match == TRUE) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,FALSE,-1);
    }

    g_free(name);
    g_free(version);
    g_free(location);
    g_free(series);
    valid = gtk_tree_model_iter_next(base_model,&iter);
  }

  if (series_regex != NULL)
    slapt_free_regex(series_regex);

  slapt_free_pkg_list(a_matches);
  slapt_free_pkg_list(i_matches);
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
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  if (gtk_tree_selection_get_selected(selection,(GtkTreeModel **)&package_model, &iter)) {
    gchar *p_name,*p_version,*p_location;
    slapt_pkg_info_t *pkg;

    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &p_name, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &p_version, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &p_location, -1);

    pkg = slapt_get_pkg_by_details(all,p_name,p_version,p_location);
    if (pkg != NULL) {
      fillin_pkg_details(pkg);
      build_package_action_menu(pkg);
    }else{
      pkg = slapt_get_pkg_by_details(installed,p_name,p_version,p_location);
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

static void fillin_pkg_details (slapt_pkg_info_t *pkg)
{
  gchar *short_desc;
  GtkTextBuffer *pkg_full_desc;
  GtkTextBuffer *pkg_changelog;
  GtkTextBuffer *pkg_filelist;
  GtkTreeStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GList *columns;
  guint i;
  GtkWidget *treeview = lookup_widget(gslapt,"dep_conf_sug_treeview");
  slapt_pkg_info_t *latest_pkg = slapt_get_newest_pkg(all,pkg->name);
  slapt_pkg_info_t *installed_pkg = slapt_get_newest_pkg(installed,pkg->name);
  slapt_pkg_upgrade_t *pkg_upgrade = NULL;
  char *clean_desc = NULL, *changelog = NULL, *filelist = NULL, *location = NULL;
  const char *priority_str = NULL;

  /* set package details */
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_name")),pkg->name);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_version")),pkg->version);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_source")),pkg->mirror);
  short_desc = slapt_gen_short_pkg_description(pkg);
  if (short_desc != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_description")),short_desc);
    free(short_desc);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_description")),"");
  }
  location = gslapt_series_map_lookup(gslapt_series_map, pkg->location);
  if (location != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_location")),location);
    free(location);
  } else {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_location")),pkg->location);
  }

  priority_str = slapt_priority_to_str(pkg->priority);
  gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_priority")),priority_str);

  /* dependency information tab */
  store = gtk_tree_store_new(1,G_TYPE_STRING);

  if (pkg->installed == SLAPT_FALSE) {

    gtk_tree_store_append(store,&iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("<b>Required:</b>"),-1);

    if (pkg->required != NULL && strlen(pkg->required) > 2) {
      GtkTreeIter child_iter;
      slapt_list_t *dependencies = slapt_parse_delimited_list(pkg->required,',');
      unsigned int i = 0;

      for (i = 0; i < dependencies->count; i++) {
        char *buffer = strdup(dependencies->items[i]);
        gtk_tree_store_append(store,&child_iter,&iter);
        gtk_tree_store_set(store,&child_iter,0,buffer,-1);
        free(buffer);
      }
      slapt_free_list(dependencies);
    }

    gtk_tree_store_append(store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("<b>Conflicts:</b>"), -1);

    if (pkg->conflicts != NULL && strlen(pkg->conflicts) > 2) {
      GtkTreeIter child_iter;
      slapt_list_t *conflicts = slapt_parse_delimited_list(pkg->conflicts,',');
      unsigned int i = 0;

      for (i = 0; i < conflicts->count; i++) {
        char *buffer = strdup(conflicts->items[i]);
        gtk_tree_store_append(store,&child_iter,&iter);
        gtk_tree_store_set(store,&child_iter,0,buffer,-1);
        free(buffer); 
      }
      slapt_free_list(conflicts);
    }

    gtk_tree_store_append(store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("<b>Suggests:</b>"),-1);

    if (pkg->suggests != NULL && strlen(pkg->suggests) > 2) {
      GtkTreeIter child_iter;
      unsigned int i = 0;
      slapt_list_t *suggestions = slapt_parse_delimited_list(pkg->suggests,',');

      for (i = 0; i < suggestions->count; i++) {
        char *buffer = strdup(suggestions->items[i]);
        gtk_tree_store_append(store,&child_iter,&iter);
        gtk_tree_store_set(store,&child_iter,0,buffer,-1);
        free(buffer);
      }
      slapt_free_list(suggestions);

    }

  } else { /* installed */
    gtk_tree_store_append(store,&iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("No dependency information available"),-1);
  }

  columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(treeview));
  for (i = 0; i < g_list_length(columns); i++ ) {
    GtkTreeViewColumn *my_column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
    if ( my_column != NULL ) {
      gtk_tree_view_remove_column(GTK_TREE_VIEW(treeview),my_column);
    }
  }
  g_list_free(columns);

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Source"), renderer,
    "markup", 0, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));
  gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));

  /* description tab */
  pkg_full_desc = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(gslapt,"pkg_description_textview")));
  clean_desc = strdup(pkg->description);
  slapt_clean_description(clean_desc,pkg->name);
  gtk_text_buffer_set_text(pkg_full_desc, clean_desc, -1);

  if ( clean_desc != NULL ) 
    free(clean_desc);

  /* changelog tab */
  pkg_changelog = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(gslapt,"pkg_changelog_textview")));
  if ((changelog = slapt_get_pkg_changelog(pkg)) != NULL) {
    if (!g_utf8_validate(changelog, -1, NULL)) {
      char *converted = g_convert(changelog, strlen(changelog), "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
      if (converted != NULL) {
        free(changelog);
        changelog = converted;
      }
    }
    gtk_text_buffer_set_text(pkg_changelog, changelog, -1);
    free(changelog);
  } else {
    gtk_text_buffer_set_text(pkg_changelog, _("No changelog information available"), -1);
  }

  /* file list tab */
  pkg_filelist = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(gslapt,"pkg_filelist_textview")));
  if ((filelist = slapt_get_pkg_filelist(pkg)) != NULL) {
    if (!g_utf8_validate(filelist, -1, NULL)) {
      char *converted = g_convert(filelist, strlen(filelist), "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
      if (converted != NULL) {
        free(filelist);
        filelist = converted;
      }
    }
    gtk_text_buffer_set_text(pkg_filelist, filelist, -1);
    free(filelist);
  } else {
    gtk_text_buffer_set_text(pkg_filelist, _("The list of installed files is only available for installed packages"), -1);
  }

  /* set status */
  if ((slapt_get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) != NULL) ||
  slapt_is_excluded(global_config,pkg) == 1) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("Excluded"));
  } else if (slapt_get_exact_pkg(trans->remove_pkgs,pkg->name,pkg->version) != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Removed"));
  } else if (slapt_get_exact_pkg(trans->install_pkgs,pkg->name,pkg->version) != NULL) {
    gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Installed"));
  } else if ( (pkg_upgrade = lsearch_upgrade_transaction(trans,pkg)) != NULL) {
    if (slapt_cmp_pkgs(pkg, pkg_upgrade->installed) == 0 &&
        slapt_cmp_pkg_versions(pkg_upgrade->upgrade->version,pkg_upgrade->installed->version) == 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Re-Installed"));
    } else if (slapt_cmp_pkgs(latest_pkg, pkg_upgrade->upgrade) > 0 && slapt_cmp_pkg_versions(pkg->version, pkg_upgrade->upgrade->version) > 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Downgraded"));
    } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) < 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Upgraded"));
    } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) == 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Installed"));
    } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) > 0) {
      gtk_label_set_text(GTK_LABEL(lookup_widget(gslapt,"pkg_info_status")),(gchar *)_("To be Downgraded"));
    }
  } else if (slapt_get_exact_pkg(installed,pkg->name,pkg->version) != NULL) {
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

static void get_package_data (void)
{
  GtkLabel *progress_action_label,
           *progress_message_label;
  GtkProgressBar *p_bar,
                 *dl_bar;
  guint i,context_id;
  gfloat dl_files = 0.0,
         dl_count = 0.0;
  ssize_t bytes_read;
  size_t getline_len = 0;
  FILE *pkg_list_fh;
  slapt_pkg_list_t *new_pkgs = slapt_init_pkg_list();
  new_pkgs->free_pkgs = TRUE;

  progress_window = create_dl_progress_window();
  gtk_window_set_title(GTK_WINDOW(progress_window),(gchar *)_("Progress"));
  p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
  dl_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress"));
  progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
  progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));

  gdk_threads_enter();
  lock_toolbar_buttons();
  context_id = gslapt_set_status((gchar *)_("Checking for new package data..."));
  gtk_widget_show(progress_window);
  gdk_threads_leave();

  #ifdef SLAPT_HAS_GPGME
    dl_files = (global_config->sources->count * 5.0 );
  #else
    dl_files = (global_config->sources->count * 4.0 );
  #endif

  if (_cancelled == 1) {
    G_LOCK(_cancelled);
    _cancelled = 0;
    G_UNLOCK(_cancelled);
    gdk_threads_enter();
    gslapt_clear_status(context_id);
    gtk_widget_destroy(progress_window);
    unlock_toolbar_buttons();
    gdk_threads_leave();
    return;
  }

  /* go through each package source and download the meta data */
  for (i = 0; i < global_config->sources->count; i++) {
    slapt_pkg_list_t *available_pkgs = NULL;
    slapt_pkg_list_t *patch_pkgs = NULL;
    FILE *tmp_checksum_f = NULL;
    #ifdef SLAPT_HAS_GPGME
    FILE *tmp_signature_f = NULL;
    #endif
    unsigned int compressed = 0;
    SLAPT_PRIORITY_T source_priority = global_config->sources->src[i]->priority;

    if (global_config->sources->src[i]->disabled == SLAPT_TRUE)
      continue;

    if (_cancelled == 1) {
      G_LOCK(_cancelled);
      _cancelled = 0;
      G_UNLOCK(_cancelled);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

    gdk_threads_enter();
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_message_label,global_config->sources->src[i]->url);
    gtk_label_set_text(progress_action_label,(gchar *)_("Retrieving package data..."));
    gdk_threads_leave();

    /* download our SLAPT_PKG_LIST */
    available_pkgs =
      slapt_get_pkg_source_packages(global_config,
                                    global_config->sources->src[i]->url,
                                    &compressed);

    /* make sure we found a package listing */
    if (available_pkgs == NULL) {
      gboolean continue_anyway = FALSE;

      gdk_threads_enter();

      if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
        gtk_widget_destroy(progress_window);
        unlock_toolbar_buttons();
        gslapt_clear_status(context_id);
      } else {
        /* prompt the user to continue downloading package sources */
        GtkWidget *q = create_source_failed_dialog();
        gtk_label_set_text(
          GTK_LABEL(lookup_widget(q,"failed_source_label")),
          global_config->sources->src[i]->url
        );

        gint result = gtk_dialog_run(GTK_DIALOG(q));
        if (result == GTK_RESPONSE_YES) {
          /* we'll disable this source and continue on */
          /* this is only disabled for the current session since
             slapt_write_rc_config() is not called */
          continue_anyway = TRUE;
          global_config->sources->src[i]->disabled = SLAPT_TRUE;
        } else {
          gtk_widget_destroy(progress_window);
          unlock_toolbar_buttons();
          gslapt_clear_status(context_id);
        }
        gtk_widget_destroy(q);

      }

      gdk_threads_leave();

      if (continue_anyway == TRUE)
        continue;
      else
        return;
    }

    ++dl_count;

    if (_cancelled == 1) {
      G_LOCK(_cancelled);
      _cancelled = 0;
      G_UNLOCK(_cancelled);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_action_label,(gchar *)_("Retrieving patch list..."));
    gdk_threads_leave();


    /* download SLAPT_PATCHES_LIST */
    patch_pkgs =
      slapt_get_pkg_source_patches(global_config,
                                   global_config->sources->src[i]->url,
                                   &compressed);


    if (_cancelled == 1) {
      G_LOCK(_cancelled);
      _cancelled = 0;
      G_UNLOCK(_cancelled);
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      gdk_threads_leave();
      return;
    }

    ++dl_count;

    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_action_label,(gchar *)_("Retrieving checksum list..."));
    gdk_threads_leave();


    /* download checksum file */
    tmp_checksum_f =
      slapt_get_pkg_source_checksums(global_config,
                                     global_config->sources->src[i]->url,
                                     &compressed);

    if (tmp_checksum_f == NULL) {
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      unlock_toolbar_buttons();
      if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
      } else {
        notify((gchar *)_("Source download failed"),global_config->sources->src[i]->url);
      }
      gdk_threads_leave();
      return;
    }

    ++dl_count;

    #ifdef SLAPT_HAS_GPGME
    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_action_label,(gchar *)_("Retrieving checksum signature..."));
    gdk_threads_leave();

    tmp_signature_f = slapt_get_pkg_source_checksums_signature (global_config,
                                                                global_config->sources->src[i]->url,
                                                                &compressed);

    if (tmp_signature_f == NULL) {
      if (_cancelled == 1) {
        gdk_threads_enter();
        gslapt_clear_status(context_id);
        gtk_widget_destroy(progress_window);
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
        unlock_toolbar_buttons();
        gdk_threads_leave();
        return;
      }
    } else {
      FILE *tmp_checksum_to_verify_f = NULL;

      /* if we downloaded the compressed checksums, open it raw (w/o gunzippign) */
      if (compressed == 1) {
        char *filename = slapt_gen_filename_from_url(global_config->sources->src[i]->url,
                                                     SLAPT_CHECKSUM_FILE_GZ);
        tmp_checksum_to_verify_f = slapt_open_file(filename,"r");
        free(filename);
      } else {
        tmp_checksum_to_verify_f = tmp_checksum_f;
      }

      if (tmp_checksum_to_verify_f != NULL) {
        slapt_code_t verified = SLAPT_CHECKSUMS_NOT_VERIFIED;
        gdk_threads_enter();
        gtk_label_set_text(progress_action_label,(gchar *)_("Verifying checksum signature..."));
        gdk_threads_leave();
        verified = slapt_gpg_verify_checksums(tmp_checksum_to_verify_f, tmp_signature_f);
        if (verified == SLAPT_CHECKSUMS_NOT_VERIFIED) {
          fclose(tmp_checksum_f);
          fclose(tmp_signature_f);
          if (compressed == 1)
            fclose(tmp_checksum_to_verify_f);
          gdk_threads_enter();
          gslapt_clear_status(context_id);
          gtk_widget_destroy(progress_window);
          unlock_toolbar_buttons();
          notify((gchar *)_("GPG Key verification failed"),global_config->sources->src[i]->url);
          gdk_threads_leave();
          return;
        }
      }

      fclose(tmp_signature_f);

      /* if we opened the raw gzipped checksums, close it here */
      if (compressed == 1)
        fclose(tmp_checksum_to_verify_f);
      else
        rewind(tmp_checksum_f);
    }

    ++dl_count;
    #endif

    /* download changelog file */
    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_action_label,(gchar *)_("Retrieving ChangeLog.txt..."));
    gdk_threads_leave();

    slapt_get_pkg_source_changelog(global_config,
                                   global_config->sources->src[i]->url,
                                   &compressed);



    gdk_threads_enter();
    gtk_progress_bar_set_fraction(p_bar,((dl_count * 100)/dl_files)/100);
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_action_label,(gchar *)_("Reading Package Lists..."));
    gdk_threads_leave();

    if (available_pkgs) {
      unsigned int pkg_i;

      /* map packages to md5sums */
      slapt_get_md5sums(available_pkgs,tmp_checksum_f);

      /* put these into our new package list */
      for (pkg_i = 0; pkg_i < available_pkgs->pkg_count; ++pkg_i) {
        /* honor the mirror if it was set in the PACKAGES.TXT */
        if (available_pkgs->pkgs[pkg_i]->mirror == NULL ||
            strlen(available_pkgs->pkgs[pkg_i]->mirror) == 0) {
          available_pkgs->pkgs[pkg_i]->mirror = strdup(global_config->sources->src[i]->url);
        }
        /* set the priority of the package based on the source */
        available_pkgs->pkgs[pkg_i]->priority = source_priority;
        slapt_add_pkg_to_pkg_list(new_pkgs,available_pkgs->pkgs[pkg_i]);
      }

      /* don't free the slapt_pkg_info_t objects
         as they are now part of new_pkgs */
      available_pkgs->free_pkgs = FALSE;
      slapt_free_pkg_list(available_pkgs);
    }

    if (patch_pkgs) {
      unsigned int pkg_i;

      /* map packages to md5sums */
      slapt_get_md5sums(patch_pkgs,tmp_checksum_f);

      /* put these into our new package list */
      for (pkg_i = 0; pkg_i < patch_pkgs->pkg_count; ++pkg_i) {
        /* honor the mirror if it was set in the PACKAGES.TXT */
        if (patch_pkgs->pkgs[pkg_i]->mirror == NULL ||
            strlen(patch_pkgs->pkgs[pkg_i]->mirror) == 0) {
          patch_pkgs->pkgs[pkg_i]->mirror = strdup(global_config->sources->src[i]->url);
        }
        /* set the priority of the package based on the source, plus 1 for the patch priority */
        if (global_config->use_priority == SLAPT_TRUE)
          patch_pkgs->pkgs[pkg_i]->priority = source_priority + 1;
        else
          patch_pkgs->pkgs[pkg_i]->priority = source_priority;
        slapt_add_pkg_to_pkg_list(new_pkgs,patch_pkgs->pkgs[pkg_i]);
      }

      /* don't free the slapt_pkg_info_t objects
         as they are now part of new_pkgs */
      patch_pkgs->free_pkgs = FALSE;
      slapt_free_pkg_list(patch_pkgs);
    }

    fclose(tmp_checksum_f);

  }/* end for loop */

  /* if all our downloads where a success, write to SLAPT_PKG_LIST_L */
  if ( (pkg_list_fh = slapt_open_file(SLAPT_PKG_LIST_L,"w+")) == NULL )
    exit(1);

  slapt_write_pkg_data(NULL,pkg_list_fh,new_pkgs);

  fclose(pkg_list_fh);

  slapt_free_pkg_list(new_pkgs);

  /* reset our currently selected packages */
  slapt_free_transaction(trans);
  trans = slapt_init_transaction();

  gdk_threads_enter();
  unlock_toolbar_buttons();
  rebuild_treeviews(progress_window,TRUE);
  gslapt_clear_status(context_id);
  gtk_widget_destroy(progress_window);
  gdk_threads_leave();

}

int gtk_progress_callback(void *data, double dltotal, double dlnow,
                          double ultotal, double ulnow)
{
  GtkProgressBar *p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress"));
  double perc = 1.0;
  struct slapt_progress_data *cb_data = (struct slapt_progress_data *)data;
  time_t now = time(NULL);
  double elapsed = now - cb_data->start;
  double speed = dlnow / (elapsed > 0 ? elapsed : 1);
  gchar *status = NULL;

  if (_cancelled == 1) {
    return -1;
  }

  if (p_bar == NULL) {
    return -1;
  }

  if ( dltotal != 0.0 )
    perc = ((dlnow * 100)/dltotal)/100;

  status = g_strdup_printf((gchar *)_("Download rate: %.0f%s/s"),
            (speed > 1000) ? (speed > 1000000) ? speed / 1000000 : speed / 1000 : speed,
            (speed > 1000) ? (speed > 1000000) ? "M" : "k" : "b");

  gdk_threads_enter();
  gtk_progress_bar_set_fraction(p_bar,perc);
  gtk_label_set_text(GTK_LABEL(lookup_widget(progress_window,"progress_dl_speed")),status);
  gdk_threads_leave();

  g_free(status);

  return 0;
}

static void rebuild_treeviews (GtkWidget *current_window,gboolean reload)
{
  GtkWidget *treeview;
  GtkListStore *store;
  GtkTreeModelFilter *filter_model;
  GtkTreeModelSort *package_model;
  const gchar *search_text = gtk_entry_get_text(
    GTK_ENTRY(lookup_widget(gslapt,"search_entry")));

  set_busy_cursor();

  if (reload == TRUE) {
    slapt_pkg_list_t *all_ptr,*installed_ptr;

    installed_ptr = installed;
    all_ptr = all;

    installed = slapt_get_installed_pkgs();
    all = slapt_get_available_pkgs();

    slapt_free_pkg_list(installed_ptr);
    slapt_free_pkg_list(all_ptr);
  }

  treeview = (GtkWidget *)lookup_widget(gslapt,"pkg_listing_treeview");
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
  gtk_list_store_clear(store);

  if (reload == TRUE) {
    gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")),"");
  }

  rebuild_package_action_menu();
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_all_packages_menu")), TRUE);
  build_package_treeviewlist(treeview);

  if ((reload == FALSE) && (strcmp(search_text,"") != 0)) {
    gchar *search = g_strdup(search_text);
    build_searched_treeviewlist(GTK_WIDGET(treeview),search);
    g_free(search);
  }
}

static guint gslapt_set_status (const gchar *msg)
{
  guint context_id;
  GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));
  context_id = gtk_statusbar_get_context_id(bar,msg);

  gtk_statusbar_push(bar,context_id,msg);

  return context_id;
}

static void gslapt_clear_status (guint context_id)
{
  GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));

  gtk_statusbar_pop(bar,context_id);
}

static void lock_toolbar_buttons (void)
{
  gtk_widget_set_sensitive(lookup_widget(gslapt,"top_menubar"),FALSE);

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_update_button"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_upgrade_button"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"),FALSE);
}

static void unlock_toolbar_buttons (void)
{

  gtk_widget_set_sensitive(lookup_widget(gslapt,"top_menubar"),TRUE);

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_update_button"),TRUE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_upgrade_button"),TRUE);

  if (
    trans->upgrade_pkgs->pkg_count != 0
    || trans->remove_pkgs->pkg_count != 0
    || trans->install_pkgs->pkg_count != 0
  ) {
    set_execute_active();
  }

}

static void lhandle_transaction (GtkWidget *w)
{
  GtkCheckButton *dl_only_checkbutton;
  gboolean dl_only = FALSE;
  slapt_pkg_list_t *installed_ptr;

  gdk_threads_enter();
  lock_toolbar_buttons();

  dl_only_checkbutton = GTK_CHECK_BUTTON(lookup_widget(w,"download_only_checkbutton"));
  dl_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dl_only_checkbutton));
  gtk_widget_destroy(w);
  gdk_threads_leave();

  /* download the pkgs */
  if ( trans->install_pkgs->pkg_count > 0 || trans->upgrade_pkgs->pkg_count > 0 ) {
    const char *err = download_packages();
    if ( err != NULL || _cancelled == 1) {

      gdk_threads_enter();
      if (err != NULL && _cancelled == 0) {
        notify((gchar *)_("Error"),(gchar *)err);
      }
      unlock_toolbar_buttons();
      gdk_threads_leave();

      if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
      }
      return;
    }

  }

  /* return early if download_only is set */
  if ( dl_only == TRUE ) {
    slapt_free_transaction(trans);
    trans = slapt_init_transaction();
    gdk_threads_enter();
    unlock_toolbar_buttons();
    rebuild_treeviews(NULL,FALSE);
    clear_execute_active();
    gdk_threads_leave();
    return;
  }

  if (_cancelled == 1) {
    G_LOCK(_cancelled);
    _cancelled = 0;
    G_UNLOCK(_cancelled);
    gdk_threads_enter();
    unlock_toolbar_buttons();
    rebuild_treeviews(NULL,FALSE);
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
  gdk_threads_enter();
  clear_execute_active();
  set_busy_cursor();
  gdk_threads_leave();

  slapt_free_transaction(trans);
  trans = slapt_init_transaction();
  /* rebuild the installed list */
  installed_ptr = installed;
  installed = slapt_get_installed_pkgs();
  slapt_free_pkg_list(installed_ptr);

  gdk_threads_enter();
  /* reset cursor */
  rebuild_treeviews(NULL,FALSE);
  rebuild_package_action_menu();
  unlock_toolbar_buttons();
  unset_busy_cursor();
  notify((gchar *)_("Completed actions"),(gchar *)_("Successfully executed all actions."));
  gdk_threads_leave();

}

void transaction_okbutton_clicked (GtkWidget *w, gpointer user_data)
{
  GThread *gdp;

  gdp = g_thread_create((GThreadFunc)lhandle_transaction,w,FALSE,NULL);

  return;

}

static void build_sources_treeviewlist(GtkWidget *treeview)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  guint i = 0;
  GdkPixbuf *enabled_status_icon   = create_pixbuf("pkg_action_installed.png");
  GdkPixbuf *disabled_status_icon  = create_pixbuf("pkg_action_available.png");
  gboolean enabled = TRUE;

  store = gtk_list_store_new (
    5,
    GDK_TYPE_PIXBUF,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_UINT
  );

  for (i = 0; i < global_config->sources->count; ++i) {
    GdkPixbuf *status_icon;
    const char *priority_str;

    if ( global_config->sources->src[i]->url == NULL )
      continue;

    if (global_config->sources->src[i]->disabled == SLAPT_TRUE) {
      enabled = FALSE;
      status_icon = disabled_status_icon;
    } else {
      enabled = TRUE;
      status_icon = enabled_status_icon;
    }

    priority_str = slapt_priority_to_str(global_config->sources->src[i]->priority);

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store,&iter,
      0,status_icon,
      1,global_config->sources->src[i]->url,
      2,enabled,
      3,priority_str,
      4,global_config->sources->src[i]->priority,
      -1);

  }

  gdk_pixbuf_unref(enabled_status_icon);
  gdk_pixbuf_unref(disabled_status_icon);

  /* column for enabled status */
  renderer = gtk_cell_renderer_pixbuf_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Enabled"), renderer,
    "pixbuf", 0, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for url */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Source"), renderer,
    "text", 1, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* enabled/disabled bool column */
  renderer = gtk_cell_renderer_toggle_new();
  column = gtk_tree_view_column_new_with_attributes((gchar *)_("Visible"),renderer,
    "radio",2,NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  /* priority column */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes((gchar *)_("Priority"),renderer,
    "text",3,NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_handlers_disconnect_by_func(G_OBJECT(treeview),toggle_source_status,NULL);
  g_signal_connect(G_OBJECT(treeview),"cursor-changed",
                   G_CALLBACK(toggle_source_status),NULL);

}

static void build_exclude_treeviewlist(GtkWidget *treeview)
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

    if ( global_config->exclude_list->items[i] == NULL )
      continue;

    gtk_list_store_append (store, &iter);
    gtk_list_store_set(store,&iter,0,global_config->exclude_list->items[i],-1);
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

static int populate_transaction_window (GtkWidget *trans_window)
{
  GtkTreeView *summary_treeview;
  GtkTreeStore *store;
  GtkTreeIter iter,child_iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkLabel *sum_pkg_num,*sum_dl_size,*sum_free_space;
  guint i;
  double dl_size = 0,free_space = 0,already_dl_size = 0;
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
      gchar *err = g_strdup_printf("%s: Depends: %s",
        trans->missing_err->errs[i]->pkg,
        trans->missing_err->errs[i]->error
      );

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,err,-1);
      g_free(err);
    }
  }

  if ( trans->conflict_err->err_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Package conflicts"),-1);

    for (i = 0; i < trans->conflict_err->err_count;++i) {
      gchar *err = g_strdup_printf("%s%s%s%s",
               trans->conflict_err->errs[i]->error,
               (gchar *)_(", which is required by "),
               trans->conflict_err->errs[i]->pkg,
               (gchar *)_(", is excluded")
      );

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store,&child_iter,0,err,-1);
      g_free(err);
    }
  }

  if ( trans->exclude_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages excluded"),-1);

    for (i = 0; i < trans->exclude_pkgs->pkg_count;++i) {
      gchar *detail = g_strdup_printf("%s %s",
        trans->exclude_pkgs->pkgs[i]->name,
        trans->exclude_pkgs->pkgs[i]->version
      );

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store, &child_iter, 0, detail, -1);

      g_free(detail);
    }
  }

  if ( trans->install_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be installed"),-1);

    for (i = 0; i < trans->install_pkgs->pkg_count;++i) {
      gchar *detail = g_strdup_printf("%s %s",
        trans->install_pkgs->pkgs[i]->name,
        trans->install_pkgs->pkgs[i]->version
      );

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store, &child_iter, 0, detail, -1);
      dl_size += trans->install_pkgs->pkgs[i]->size_c;
      already_dl_size += slapt_get_pkg_file_size(global_config,trans->install_pkgs->pkgs[i])/1024;
      free_space += trans->install_pkgs->pkgs[i]->size_u;

      g_free(detail);
    }
  }

  if ( (trans->upgrade_pkgs->pkg_count - trans->upgrade_pkgs->reinstall_count) > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be upgraded"),-1);

    for (i = 0; i < trans->upgrade_pkgs->pkg_count;++i) {
      gchar *detail = g_strdup_printf("%s (%s) -> %s",
        trans->upgrade_pkgs->pkgs[i]->installed->name,
        trans->upgrade_pkgs->pkgs[i]->installed->version,
        trans->upgrade_pkgs->pkgs[i]->upgrade->version
      );

      if (trans->upgrade_pkgs->pkgs[i]->reinstall == SLAPT_TRUE) {
        g_free(detail);
        continue;
      }

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store, &child_iter, 0, detail, -1);

      dl_size += trans->upgrade_pkgs->pkgs[i]->upgrade->size_c;
      already_dl_size += slapt_get_pkg_file_size(global_config,trans->upgrade_pkgs->pkgs[i]->upgrade)/1024;
      free_space += trans->upgrade_pkgs->pkgs[i]->upgrade->size_u;
      free_space -= trans->upgrade_pkgs->pkgs[i]->installed->size_u;

      g_free(detail);
    }
  }

  if ( trans->upgrade_pkgs->reinstall_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be reinstalled"),-1);
    for (i = 0; i < trans->upgrade_pkgs->pkg_count;++i) {
      gchar *detail = g_strdup_printf("%s %s",
        trans->upgrade_pkgs->pkgs[i]->upgrade->name,
        trans->upgrade_pkgs->pkgs[i]->upgrade->version
      );

      if (trans->upgrade_pkgs->pkgs[i]->reinstall == SLAPT_FALSE) {
        g_free(detail);
        continue;
      }

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store, &child_iter, 0, detail, -1);

      dl_size += trans->upgrade_pkgs->pkgs[i]->upgrade->size_c;
      already_dl_size += slapt_get_pkg_file_size(global_config,trans->upgrade_pkgs->pkgs[i]->upgrade)/1024;
      free_space += trans->upgrade_pkgs->pkgs[i]->upgrade->size_u;
      free_space -= trans->upgrade_pkgs->pkgs[i]->installed->size_u;

      g_free(detail);
    }
  }

  if ( trans->remove_pkgs->pkg_count > 0 ) {
    gtk_tree_store_append (store, &iter,NULL);
    gtk_tree_store_set(store,&iter,0,_("Packages to be removed"),-1);

    for (i = 0; i < trans->remove_pkgs->pkg_count;++i) {
      gchar *detail = g_strdup_printf("%s %s",
        trans->remove_pkgs->pkgs[i]->name,
        trans->remove_pkgs->pkgs[i]->version
      );

      gtk_tree_store_append (store, &child_iter, &iter);
      gtk_tree_store_set(store, &child_iter, 0, detail, -1);
      free_space -= trans->remove_pkgs->pkgs[i]->size_u;

      g_free(detail);
    }
  }

  gtk_tree_view_set_model (GTK_TREE_VIEW(summary_treeview),GTK_TREE_MODEL(store));

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Package"), renderer,
    "text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(summary_treeview), column);

  snprintf(buf,512,(gchar *)_("%d upgraded, %d reinstalled, %d newly installed, %d to remove and %d not upgraded."),
    trans->upgrade_pkgs->pkg_count - trans->upgrade_pkgs->reinstall_count,
    trans->upgrade_pkgs->reinstall_count,
    trans->install_pkgs->pkg_count,
    trans->remove_pkgs->pkg_count,
    trans->exclude_pkgs->pkg_count
  );
  gtk_label_set_text(GTK_LABEL(sum_pkg_num),buf);

  /* if we don't have enough free space to download */
  if (slapt_disk_space_check(global_config->working_dir,dl_size - already_dl_size) == SLAPT_FALSE) {
    notify((gchar *)_("Error"),(gchar *)_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
    return -1;
  }
  /* if we don't have enough free space to install on / */
  if (slapt_disk_space_check("/",free_space) == SLAPT_FALSE) {
    notify((gchar *)_("Error"),(gchar *)_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
    return -1;
  }

  if ( already_dl_size > 0 ) {
    double need_to_dl = dl_size - already_dl_size;
    snprintf(buf,512,(gchar *)_("Need to get %.0f%s/%.0f%s of archives.\n"),
      (need_to_dl > 1024 ) ? need_to_dl / 1024
        : need_to_dl,
      (need_to_dl > 1024 ) ? "MB" : "kB",
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,(gchar *)_("Need to get %.0f%s of archives."),
      (dl_size > 1024 ) ? dl_size / 1024 : dl_size,
      (dl_size > 1024 ) ? "MB" : "kB"
    );
  }
  gtk_label_set_text(GTK_LABEL(sum_dl_size),buf);

  if ( free_space < 0 ) {
    free_space *= -1;
    snprintf(buf,512,(gchar *)_("After unpacking %.0f%s disk space will be freed."),
      (free_space > 1024 ) ? free_space / 1024
        : free_space,
      (free_space > 1024 ) ? "MB" : "kB"
    );
  }else{
    snprintf(buf,512,(gchar *)_("After unpacking %.0f%s of additional disk space will be used."),
      (free_space > 1024 ) ? free_space / 1024 : free_space,
        (free_space > 1024 ) ? "MB" : "kB"
    );
  }
  gtk_label_set_text(GTK_LABEL(sum_free_space),buf);

  return 0;
}


static void mark_upgrade_packages (void)
{
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  guint i,mark_count = 0;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
  
  for (i = 0; i < installed->pkg_count;i++) {
    slapt_pkg_info_t *update_pkg = NULL;
    slapt_pkg_info_t *newer_installed_pkg = NULL;

    /*
      we need to see if there is another installed
      package that is newer than this one
    */
    if ( (newer_installed_pkg = slapt_get_newest_pkg(installed,installed->pkgs[i]->name)) != NULL ) {

      if ( slapt_cmp_pkgs(installed->pkgs[i],newer_installed_pkg) < 0 )
        continue;

    }

    /* see if we have an available update for the pkg */
    update_pkg = slapt_get_newest_pkg(
      all,
      installed->pkgs[i]->name
    );
    if ( update_pkg != NULL ) {
      int cmp_r = 0;

      /* if the update has a newer version, attempt to upgrade */
      cmp_r = slapt_cmp_pkgs(installed->pkgs[i],update_pkg);
      /* either it's greater, or we want to reinstall */
      if ( cmp_r < 0 || (global_config->re_install == TRUE) ) {

        if ((slapt_is_excluded(global_config,update_pkg) == 1)
          || (slapt_is_excluded(global_config,installed->pkgs[i]) == 1)
        ) {
          slapt_add_exclude_to_transaction(trans,update_pkg);
        }else{
          slapt_pkg_list_t *conflicts = slapt_is_conflicted(trans,all,installed,update_pkg);

          /* if all deps are added and there is no conflicts, add on */
          if (
            (ladd_deps_to_trans(trans,all,installed,update_pkg) == 0)
            && (conflicts->pkg_count == 0)
          ) {

            if (global_config->re_install == TRUE)
              slapt_add_reinstall_to_transaction(trans,installed->pkgs[i],update_pkg);
            else
              slapt_add_upgrade_to_transaction(trans,installed->pkgs[i],update_pkg);

            if (set_iter_to_pkg(base_model,&iter,update_pkg)) {
              set_iter_for_upgrade(base_model, &iter, update_pkg);
            }
            ++mark_count;
          }else{
            /* otherwise exclude */
            slapt_add_exclude_to_transaction(trans,update_pkg);
          }
          slapt_free_pkg_list(conflicts);
        }

      }

    }/* end upgrade pkg found */

  }/* end for */

  if (mark_count == 0) {
    notify((gchar *)_("Up to Date"),
      (gchar *)_("<span weight=\"bold\" size=\"large\">No updates available</span>")); 
  }
}

char *download_packages (void)
{
  GtkLabel *progress_action_label,*progress_message_label,*progress_pkg_desc;
  GtkProgressBar *p_bar;
  guint i,context_id;
  gfloat pkgs_to_dl = 0.0,count = 0.0;
  const char *err = NULL;

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
    return NULL;
  }

  for (i = 0; i < trans->install_pkgs->pkg_count;++i) {

    guint msg_len = strlen(trans->install_pkgs->pkgs[i]->name)
        + strlen("-") + strlen(trans->install_pkgs->pkgs[i]->version)
        + strlen(".") + strlen(trans->install_pkgs->pkgs[i]->file_ext);
    gchar *msg = slapt_malloc(msg_len * sizeof *msg);
    gchar dl_size[20];

    snprintf(msg,
      strlen(trans->install_pkgs->pkgs[i]->name)
      + strlen("-")
      + strlen(trans->install_pkgs->pkgs[i]->version)
      + strlen(".") + strlen(trans->install_pkgs->pkgs[i]->file_ext),
      "%s-%s%s",
      trans->install_pkgs->pkgs[i]->name,
      trans->install_pkgs->pkgs[i]->version,
      trans->install_pkgs->pkgs[i]->file_ext
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
      return NULL;
    }

    err = slapt_download_pkg(global_config,trans->install_pkgs->pkgs[i], NULL);
    if (err) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return g_strdup_printf(_("Failed to download %s: %s"),
                        trans->install_pkgs->pkgs[i]->name, err);
    }
    ++count;
  }
  for (i = 0; i < trans->upgrade_pkgs->pkg_count;++i) {

    guint msg_len = strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->name)
        + strlen("-") + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->version)
        + strlen(".") + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->file_ext);
    gchar *msg = slapt_malloc( sizeof *msg * msg_len);
    gchar dl_size[20];

    snprintf(msg,
      strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->name)
      + strlen("-")
      + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->version)
      + strlen(".") + strlen(trans->upgrade_pkgs->pkgs[i]->upgrade->file_ext),
      "%s-%s%s",
      trans->upgrade_pkgs->pkgs[i]->upgrade->name,
      trans->upgrade_pkgs->pkgs[i]->upgrade->version,
      trans->upgrade_pkgs->pkgs[i]->upgrade->file_ext
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
      return NULL;
    }

    err = slapt_download_pkg(global_config,trans->upgrade_pkgs->pkgs[i]->upgrade, NULL);
    if (err) {
      gdk_threads_enter();
      gtk_widget_destroy(progress_window);
      gslapt_clear_status(context_id);
      gdk_threads_leave();
      return g_strdup_printf("Failed to download %s: %s",
                        trans->upgrade_pkgs->pkgs[i]->upgrade->name, err);
    }
    ++count;
  }

  gdk_threads_enter();
  gtk_widget_destroy(progress_window);
  gslapt_clear_status(context_id);
  gdk_threads_leave();
  return NULL;
}

static gboolean install_packages (void)
{
  GtkLabel *progress_action_label,*progress_message_label,*progress_pkg_desc;
  GtkProgressBar *p_bar;
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
  gdk_threads_leave();

  for (i = 0; i < trans->remove_pkgs->pkg_count;++i) {
    char *clean_desc = strdup(trans->remove_pkgs->pkgs[i]->description);
    slapt_clean_description(clean_desc,trans->remove_pkgs->pkgs[i]->name);

    gdk_threads_enter();

    context_id = gslapt_set_status((gchar *)_("Removing packages..."));
    gtk_label_set_text(progress_pkg_desc,clean_desc);

    if ( clean_desc != NULL)
      free(clean_desc);

    gtk_label_set_text(progress_action_label,(gchar *)_("Uninstalling..."));
    gtk_label_set_text(progress_message_label,trans->remove_pkgs->pkgs[i]->name);
    gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->remove_pkgs->pkg_count)/100);
    gdk_threads_leave();

    if (slapt_remove_pkg(global_config,trans->remove_pkgs->pkgs[i]) == -1) {
      gdk_threads_enter();
      gslapt_clear_status(context_id);
      gtk_widget_destroy(progress_window);
      gdk_threads_leave();
      return FALSE;
    }
    gdk_threads_enter();
    gslapt_clear_status(context_id);
    gdk_threads_leave();
    ++count;
  }

  /* reset progress bar */
  gdk_threads_enter();
  gtk_progress_bar_set_fraction(p_bar,0.0);
  gdk_threads_leave();

  /* now for the installs and upgrades */
  count = 0.0;
  for (i = 0;i < trans->queue->count; ++i) {
    gdk_threads_enter();
    context_id = gslapt_set_status((gchar *)_("Installing packages..."));
    gdk_threads_leave();

    if ( trans->queue->pkgs[i]->type == INSTALL ) {
      char *clean_desc = strdup(trans->queue->pkgs[i]->pkg.i->description);
      slapt_clean_description(clean_desc,trans->queue->pkgs[i]->pkg.i->name);

      gdk_threads_enter();

      gtk_label_set_text(progress_pkg_desc,clean_desc);
      if ( clean_desc != NULL ) 
        free(clean_desc);

      gtk_label_set_text(progress_action_label,(gchar *)_("Installing..."));
      gtk_label_set_text(progress_message_label,trans->queue->pkgs[i]->pkg.i->name);
      gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->queue->count)/100);
      gdk_threads_leave();

      if (slapt_install_pkg(global_config,trans->queue->pkgs[i]->pkg.i) == -1) {
        gdk_threads_enter();
        gslapt_clear_status(context_id);
        gtk_widget_destroy(progress_window);
        gdk_threads_leave();
        return FALSE;
      }
    }else if ( trans->queue->pkgs[i]->type == UPGRADE ) {
      char *clean_desc = strdup(trans->queue->pkgs[i]->pkg.u->upgrade->description);
      slapt_clean_description(clean_desc,trans->queue->pkgs[i]->pkg.u->upgrade->name);

      gdk_threads_enter();

      gtk_label_set_text(progress_pkg_desc,clean_desc);
      if ( clean_desc != NULL )
        free(clean_desc);

      gtk_label_set_text(progress_action_label,(gchar *)_("Upgrading..."));
      gtk_label_set_text(progress_message_label,trans->queue->pkgs[i]->pkg.u->upgrade->name);
      gtk_progress_bar_set_fraction(p_bar,((count * 100)/trans->queue->count)/100);
      gdk_threads_leave();

      if (slapt_upgrade_pkg(global_config,
                            trans->queue->pkgs[i]->pkg.u->upgrade) == -1) {
        gdk_threads_enter();
        gslapt_clear_status(context_id);
        gtk_widget_destroy(progress_window);
        gdk_threads_leave();
        return FALSE;
      }
    }
    gdk_threads_enter();
    gslapt_clear_status(context_id);
    gdk_threads_leave();
    ++count;
  }

  gdk_threads_enter();
  gtk_widget_destroy(progress_window);
  gdk_threads_leave();

  return TRUE;
}


void clean_callback (GtkWidget *widget, gpointer user_data)
{
  GThread *gpd;

  gpd = g_thread_create((GThreadFunc)slapt_clean_pkg_dir,global_config->working_dir,FALSE,NULL);

}


void preferences_sources_add (GtkWidget *w, gpointer user_data)
{
  GtkWidget *source_window      = create_source_window();
  GtkComboBox *source_priority  = GTK_COMBO_BOX(lookup_widget(source_window,"source_priority"));
  gtk_combo_box_set_active (source_priority,0);
  gtk_widget_show(source_window);
}

void preferences_sources_remove (GtkWidget *w, gpointer user_data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *source_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_sources_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (source_tree));
  GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(source_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    gtk_list_store_remove(store, &iter);
    sources_modified = TRUE;
  }

}

void preferences_sources_edit (GtkWidget *w, gpointer user_data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *source_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_sources_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (source_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    guint i = 0, priority;
    gchar *source;

    gtk_tree_model_get(model,&iter,1,&source,4,&priority,-1);

    if (source) {
      GtkWidget *source_window      = create_source_window();
      GtkEntry  *source_entry       = GTK_ENTRY(lookup_widget(source_window,"source_entry"));
      GtkComboBox *source_priority  = GTK_COMBO_BOX(lookup_widget(source_window,"source_priority"));

      g_object_set_data ( G_OBJECT(source_window), "original_url", source);
      gtk_entry_set_text(source_entry,source);
      gtk_combo_box_set_active (source_priority,convert_slapt_priority_to_gslapt_priority(priority));
      gtk_widget_show(source_window);
    }

  }

}

void preferences_on_ok_clicked (GtkWidget *w, gpointer user_data)
{
  GtkEntry *preferences_working_dir_entry = GTK_ENTRY(lookup_widget(w,"preferences_working_dir_entry"));
  const gchar *working_dir = gtk_entry_get_text(preferences_working_dir_entry);
  GtkTreeModel *model;
  GtkTreeView *tree;
  GtkTreeIter iter;
  gboolean valid;

  strncpy(
    global_config->working_dir,
    working_dir,
    strlen(working_dir)
  );
  global_config->working_dir[
    strlen(working_dir)
  ] = '\0';

  slapt_free_list(global_config->exclude_list);
  slapt_free_source_list(global_config->sources);

  global_config->exclude_list = slapt_init_list();
  global_config->sources      = slapt_init_source_list();

  tree  = GTK_TREE_VIEW(lookup_widget(w,"preferences_sources_treeview"));
  model = gtk_tree_view_get_model(tree);
  valid = gtk_tree_model_get_iter_first(model,&iter);
  while (valid)
  {
    gchar *source = NULL;
    gboolean status;
    SLAPT_PRIORITY_T priority;
    gtk_tree_model_get(model, &iter, 1, &source, 2, &status, 4, &priority, -1);

    if (source != NULL) {
      slapt_source_t *src = slapt_init_source(source);
      if (src != NULL) {

        if (status)
          src->disabled = SLAPT_FALSE;
        else
          src->disabled = SLAPT_TRUE;
  
        src->priority = priority;

        slapt_add_source(global_config->sources,src);
      }
      g_free(source);
    }

    valid = gtk_tree_model_iter_next(model, &iter);
  }

  tree  = GTK_TREE_VIEW(lookup_widget(w,"preferences_exclude_treeview"));
  model = gtk_tree_view_get_model(tree);
  valid = gtk_tree_model_get_iter_first(model,&iter);
  while (valid)
  {
    gchar *exclude = NULL;
    gtk_tree_model_get(model, &iter, 0, &exclude, -1);

    slapt_add_list_item(global_config->exclude_list, exclude);
    g_free(exclude);

    valid = gtk_tree_model_iter_next(model, &iter);
  }

  if ( slapt_write_rc_config(global_config, rc_location) != 0) {
    notify((gchar *)_("Error"),(gchar *)_("Failed to commit preferences"));
    on_gslapt_destroy(NULL,NULL);
  }

  preferences_window = NULL;
  gtk_widget_destroy(w);

  /* dialog to resync package sources */
  if (sources_modified == TRUE) {
    sources_modified = FALSE;
    if (excludes_modified == TRUE)
      excludes_modified = FALSE;
    GtkWidget *rc = create_repositories_changed();
    gtk_widget_show(rc);
  } else {
    /* rebuild package list */
    if (excludes_modified == TRUE) {
      excludes_modified = FALSE;
      rebuild_treeviews(NULL,FALSE);
    }
  }
}


void preferences_exclude_add(GtkWidget *w, gpointer user_data) 
{
  GtkEntry *new_exclude_entry = GTK_ENTRY(lookup_widget(w,"new_exclude_entry"));
  const gchar *new_exclude = gtk_entry_get_text(new_exclude_entry);
  GtkTreeView *exclude_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_exclude_treeview"));
  GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(exclude_tree));
  GtkTreeIter iter;

  if ( new_exclude == NULL || strlen(new_exclude) < 1 )
    return;

  gtk_list_store_append(store, &iter);
  gtk_list_store_set(store, &iter, 0, new_exclude, -1);

  gtk_entry_set_text(new_exclude_entry,"");

}


void preferences_exclude_remove(GtkWidget *w, gpointer user_data) 
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *exclude_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_exclude_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (exclude_tree));
  GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(exclude_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    gtk_list_store_remove(store, &iter);
  }

}


void cancel_preferences (GtkWidget *w, gpointer user_data)
{
  preferences_window = NULL;
  gtk_widget_destroy(w);
}


void cancel_transaction (GtkWidget *w, gpointer user_data)
{
  gtk_widget_destroy(w);
}

void add_pkg_for_reinstall (GtkWidget *gslapt, gpointer user_data)
{

  global_config->re_install = TRUE;
  add_pkg_for_install(gslapt,user_data);
  global_config->re_install = FALSE;

}

static void set_execute_active (void)
{

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"),TRUE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"execute1"),TRUE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"unmark_all1"),TRUE);

  if (pending_trans_context_id == 0) {
    pending_trans_context_id = gslapt_set_status((gchar *)_("Pending changes. Click execute when ready."));
  }

}

static void clear_execute_active (void)
{

  if ( pending_trans_context_id > 0 ) {
    gtk_statusbar_pop(
      GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar")),
      pending_trans_context_id
    );
    pending_trans_context_id = 0;
  }

  gtk_widget_set_sensitive(lookup_widget(gslapt,"action_bar_execute_button"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"execute1"),FALSE);
  gtk_widget_set_sensitive(lookup_widget(gslapt,"unmark_all1"),FALSE);

}

static void notify (const char *title,const char *message)
{
  GtkWidget *w = create_notification();
  gtk_window_set_title (GTK_WINDOW (w), title);
  gtk_label_set_text(GTK_LABEL(lookup_widget(w,"notification_label")),message);
  gtk_label_set_use_markup (GTK_LABEL(lookup_widget(w,"notification_label")),
                            TRUE);
  gtk_widget_show(w);
}

static gboolean pkg_action_popup_menu (GtkTreeView *treeview, gpointer data)
{
  GtkMenu *menu;
  GdkEventButton *event = (GdkEventButton *)gtk_get_current_event();
  GtkTreeViewColumn *column;
  GtkTreePath *path;

  if (event->type != GDK_BUTTON_PRESS)
    return FALSE;

  if (!gtk_tree_view_get_path_at_pos(treeview,event->x,event->y,&path,&column,NULL,NULL))
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
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  slapt_pkg_info_t *pkg = NULL;
  guint is_installed = 0,i;
  GtkTreeModelSort *package_model;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  selection = gtk_tree_view_get_selection(treeview);
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  if (gtk_tree_selection_get_selected(selection,(GtkTreeModel **)&package_model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    gchar *status = NULL;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *model;
    GtkTreeIter actual_iter, filter_iter;

    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

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

    if (slapt_get_exact_pkg(installed,pkg_name,pkg_version) != NULL)
      is_installed = 1;

    if (((pkg = slapt_get_pkg_by_details(all,pkg_name,pkg_version,pkg_location)) == NULL)) {
      pkg = slapt_get_exact_pkg(installed,pkg_name,pkg_version);
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

    /* convert sort model and iter to filter */
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model),&filter_iter,&iter);
    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    /* convert filter to regular tree */
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model),&actual_iter,&filter_iter);
    model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    if (is_installed == 1) {
      GdkPixbuf *status_icon = create_pixbuf("pkg_action_installed.png");
      gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,STATUS_ICON_COLUMN,status_icon,-1);
      status = g_strdup_printf("a%s",pkg->name);
      gdk_pixbuf_unref(status_icon);
    } else {
      GdkPixbuf *status_icon = create_pixbuf("pkg_action_available.png");
      gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,STATUS_ICON_COLUMN,status_icon,-1);
      status = g_strdup_printf("z%s",pkg->name);
      gdk_pixbuf_unref(status_icon);
    }
    gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,STATUS_COLUMN,status,-1);
    g_free(status);

    gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,MARKED_COLUMN,FALSE,-1);

    /* clear the installed version as well if this was an upgrade */
    for (i = 0; i < trans->upgrade_pkgs->pkg_count; ++i) {
      if (strcmp(trans->upgrade_pkgs->pkgs[i]->installed->name,pkg->name) == 0) {
        slapt_pkg_info_t *installed_pkg = trans->upgrade_pkgs->pkgs[i]->installed;
        slapt_pkg_info_t *upgrade_pkg = trans->upgrade_pkgs->pkgs[i]->upgrade;

        if (installed_pkg == NULL)
          continue;

        if (set_iter_to_pkg(model,&actual_iter,installed_pkg)) {
          gchar *istatus = g_strdup_printf("i%s",installed_pkg->name);
          GdkPixbuf *status_icon = create_pixbuf("pkg_action_installed.png");
          gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,STATUS_ICON_COLUMN,status_icon,-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,STATUS_COLUMN,istatus,-1);
          gtk_list_store_set(GTK_LIST_STORE(model),&actual_iter,MARKED_COLUMN,FALSE,-1);
          g_free(istatus);
          gdk_pixbuf_unref(status_icon);
        } else {
          fprintf(stderr,"failed to find iter for installed package %s-%s to unmark\n",trans->upgrade_pkgs->pkgs[i]->installed->name,trans->upgrade_pkgs->pkgs[i]->installed->version);
        }

        if (upgrade_pkg != NULL) {
          trans = slapt_remove_from_transaction(trans,upgrade_pkg);
        }

      }
    }

    trans = slapt_remove_from_transaction(trans,pkg);
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
static int ladd_deps_to_trans (slapt_transaction_t *tran, slapt_pkg_list_t *avail_pkgs,
                               slapt_pkg_list_t *installed_pkgs, slapt_pkg_info_t *pkg)
{
  unsigned int c;
  int dep_return = -1;
  slapt_pkg_list_t *deps = NULL;
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;

  if ( global_config->disable_dep_check == TRUE )
    return 0;
  if ( pkg == NULL )
    return 0;

  deps = slapt_init_pkg_list();

  dep_return = slapt_get_pkg_dependencies(
    global_config,avail_pkgs,installed_pkgs,pkg,
    deps,tran->conflict_err,tran->missing_err
  );

  /* check to see if there where issues with dep checking */
  /* exclude the package if dep check barfed */
  if ( (dep_return == -1) && (global_config->ignore_dep == FALSE) &&
      (slapt_get_exact_pkg(tran->exclude_pkgs,pkg->name,pkg->version) == NULL)
  ) {
    /* don't add exclude here... later we offer an option to install anyway */
    /* slapt_add_exclude_to_transaction(tran,pkg); */
    slapt_free_pkg_list(deps);
    return -1;
  }

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  /* loop through the deps */
  for (c = 0; c < deps->pkg_count;c++) {
    slapt_pkg_info_t *dep_installed;
    slapt_pkg_list_t *conflicts = slapt_is_conflicted(tran,avail_pkgs,
                                        installed_pkgs,deps->pkgs[c]);

    /*
     * the dep wouldn't get this far if it where excluded,
     * so we don't check for that here
     */

    if ( conflicts->pkg_count > 1 ) {
      unsigned int cindex = 0;
      for (cindex = 0; cindex < conflicts->pkg_count; cindex++) {
        slapt_pkg_info_t *conflicted_pkg = conflicts->pkgs[cindex];
        slapt_add_remove_to_transaction(tran,conflicted_pkg);
        if (set_iter_to_pkg(GTK_TREE_MODEL(base_model),&iter,conflicted_pkg)) {
          set_iter_for_remove(base_model, &iter, conflicted_pkg);
        }
      }
    }

    dep_installed = slapt_get_newest_pkg(installed_pkgs,deps->pkgs[c]->name);
    if ( dep_installed == NULL ) {
      slapt_add_install_to_transaction(tran,deps->pkgs[c]);
      if (set_iter_to_pkg(GTK_TREE_MODEL(base_model),&iter,deps->pkgs[c])) {
        set_iter_for_install(base_model, &iter, deps->pkgs[c]);
      }
    } else {
      /* add only if its a valid upgrade */
      if (slapt_cmp_pkgs(dep_installed,deps->pkgs[c]) < 0 ) {
        slapt_add_upgrade_to_transaction(tran,dep_installed,deps->pkgs[c]);
        if (set_iter_to_pkg(GTK_TREE_MODEL(base_model),&iter,deps->pkgs[c])) {
          set_iter_for_upgrade(base_model, &iter, deps->pkgs[c]);
        }
      }
    }

  }

  slapt_free_pkg_list(deps);

  return 0;
}

static int set_iter_to_pkg (GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
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

void build_treeview_columns (GtkWidget *treeview)
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

  /* column for series */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Location"), renderer,
    "text", SERIES_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, SERIES_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for short description */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Description"), renderer,
    "text", DESC_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, DESC_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* column for installed size */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Installed Size"), renderer,
    "text", SIZE_COLUMN, NULL);
  gtk_tree_view_column_set_sort_column_id (column, SIZE_COLUMN);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_resizable(column, TRUE);

  /* invisible column to sort installed by */
  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes ((gchar *)_("Installed"), renderer,
    "text", STATUS_COLUMN, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  /* column to set visibility */
  renderer = gtk_cell_renderer_toggle_new();
  column = gtk_tree_view_column_new_with_attributes((gchar *)_("Visible"),renderer,
    "radio",VISIBLE_COLUMN,NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
  gtk_tree_view_column_set_visible(column,FALSE);

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
    G_CALLBACK (show_pkg_details), NULL);

  g_signal_connect(G_OBJECT(treeview),"cursor-changed",
    G_CALLBACK(pkg_action_popup_menu),NULL);

}

static slapt_pkg_upgrade_t *lsearch_upgrade_transaction (slapt_transaction_t *tran,slapt_pkg_info_t *pkg)
{
  unsigned int i;

  /*
    lookup the package in the upgrade list, checking for the same name,version
    or location (in the case of location, either the same location for upgrade
    or installed)
  */  
  for (i = 0; i < tran->upgrade_pkgs->pkg_count;i++) {
    if (strcmp(pkg->name,tran->upgrade_pkgs->pkgs[i]->upgrade->name) == 0 &&
    strcmp(pkg->version,tran->upgrade_pkgs->pkgs[i]->upgrade->version) == 0 &&
    (strcmp(pkg->location,tran->upgrade_pkgs->pkgs[i]->upgrade->location) == 0) || 
    (strcmp(pkg->location,tran->upgrade_pkgs->pkgs[i]->installed->location) == 0)) {
      return tran->upgrade_pkgs->pkgs[i];
    }
  }
 
  return NULL;
}

void open_icon_legend (GtkObject *object, gpointer user_data)
{
  GtkWidget *icon_legend = create_icon_legend();
  gtk_widget_show(icon_legend);
}


void on_button_cancel_clicked (GtkButton *button, gpointer user_data)
{
  G_LOCK(_cancelled);
  _cancelled = 1;
  G_UNLOCK(_cancelled);
}

static void build_package_action_menu (slapt_pkg_info_t *pkg)
{
  GtkMenu *menu;
  slapt_pkg_info_t *newest_installed = NULL, *upgrade_pkg = NULL;
  guint is_installed = 0,is_newest = 1,is_exclude = 0,is_downloadable = 0,is_downgrade = 0;

  if (slapt_get_exact_pkg(installed,pkg->name,pkg->version) != NULL)
    is_installed = 1;

  /* in order to know if a package is re-installable, we have to just look at the
     name and version, not the location */
  if (slapt_get_exact_pkg(all,pkg->name,pkg->version) != NULL)
    is_downloadable = 1;

  /* find out if there is a newer available package */
  upgrade_pkg = slapt_get_newest_pkg(all,pkg->name);
  if (upgrade_pkg != NULL && slapt_cmp_pkgs(pkg,upgrade_pkg) < 0)
    is_newest = 0;

  if ((newest_installed = slapt_get_newest_pkg(installed,pkg->name)) != NULL) {
    /* this will always be true for high priority package sources */
    if (slapt_cmp_pkgs(pkg,newest_installed) < 0) {
      is_downgrade = 1;
    } else if (is_newest == 0 && slapt_cmp_pkg_versions(pkg->version,newest_installed->version) < 0) {
      /* if pkg is not the newest available version and the version is actually 
         less (and does not have a higher priority or would have been handled
         above) then we consider this a downgrade */
      is_downgrade = 1;
    } else if (slapt_cmp_pkgs(pkg,newest_installed) == 0) {
      /* maybe this isn't the exact installed package, but it's different enough
         to warrant reinstall-ability... it is questionable if this is ever
         reached */
      is_installed = 1;
    }
  }

  if ( slapt_is_excluded(global_config,pkg) == 1 
  || slapt_get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) != NULL) {
    is_exclude = 1;
  }

  menu = GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(lookup_widget(gslapt,"package1"))));

  gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"upgrade1"),FALSE);
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

  if ( slapt_search_transaction(trans,pkg->name) == 0 ) {
    if ( is_exclude == 0 ) {
      /* upgrade */
      if ( is_installed == 1 && is_newest == 0 && (slapt_search_transaction_by_pkg(trans,upgrade_pkg) == 0 ) ) {
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
    (slapt_search_transaction(trans,pkg->name) == 0)
  ) {
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"remove1"),TRUE);
  }

  if ((slapt_get_exact_pkg(trans->exclude_pkgs,pkg->name,pkg->version) == NULL) && 
    (slapt_search_transaction_by_pkg(trans,pkg) == 1)
  ) {
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(menu),"unmark1"),TRUE);
  }

}

static void rebuild_package_action_menu (void)
{
  GtkTreeView *treeview;
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModelSort *package_model;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));
  selection = gtk_tree_view_get_selection(treeview);

  if (gtk_tree_selection_get_selected(selection,(GtkTreeModel **)&package_model,&iter) == TRUE) {
    gchar *pkg_name;
    gchar *pkg_version;
    gchar *pkg_location;
    slapt_pkg_info_t *pkg = NULL;

    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
    gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

    if (pkg_name == NULL || pkg_version == NULL || pkg_location == NULL) {
      if (pkg_name != NULL)
        g_free(pkg_name);

      if (pkg_version != NULL)
        g_free(pkg_version);

      if (pkg_location != NULL)
        g_free(pkg_location);

      return;
    }

    pkg = slapt_get_pkg_by_details(all,pkg_name,pkg_version,pkg_location);
    if (pkg == NULL) {
      pkg = slapt_get_pkg_by_details(installed,pkg_name,pkg_version,pkg_location);
    }
    g_free(pkg_name);
    g_free(pkg_version);
    g_free(pkg_location);

    if (pkg != NULL) {
      build_package_action_menu(pkg);
    }

  } else {
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"upgrade1"),FALSE);
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"re_install1"),FALSE);
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"downgrade1"),FALSE);
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"install1"),FALSE);
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"remove1"),FALSE);
    gtk_widget_set_sensitive(lookup_widget(GTK_WIDGET(gslapt),"unmark1"),FALSE);
  }

}


void unmark_all_activate (GtkMenuItem *menuitem, gpointer user_data)
{

  lock_toolbar_buttons();

  /* reset our currently selected packages */
  slapt_free_transaction(trans);
  trans = slapt_init_transaction();

  rebuild_package_action_menu();
  rebuild_treeviews(NULL,FALSE);
  unlock_toolbar_buttons();
  clear_execute_active();

}

static void reset_search_list (void)
{
  gboolean view_list_all = FALSE, view_list_installed = FALSE,
           view_list_available = FALSE, view_list_marked = FALSE,
           view_list_upgradeable = FALSE;

  view_list_all         = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_all_packages_menu")));
  view_list_available   = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_available_packages_menu")));
  view_list_installed   = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_installed_packages_menu")));
  view_list_marked      = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_marked_packages_menu")));
  view_list_upgradeable = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lookup_widget(gslapt,"view_upgradeable_packages_menu")));

  if (view_list_all) {
    view_all_packages(NULL,NULL);
  } else if (view_list_installed) {
    view_installed_packages(NULL,NULL);
  } else if (view_list_available) {
    view_available_packages(NULL,NULL);
  } else if (view_list_marked) {
    view_marked_packages(NULL,NULL);
  } else if (view_list_upgradeable) {
    view_upgradeable_packages(NULL,NULL);
  }

}

GtkEntryCompletion *build_search_completions (void)
{
  GtkTreeIter iter;
  GtkTreeModel *completions;
  GtkEntryCompletion *completion;
  guint i;

  completions = GTK_TREE_MODEL(gtk_list_store_new(1,G_TYPE_STRING));

  completion = gtk_entry_completion_new();
  gtk_entry_completion_set_model(completion,completions);
  gtk_entry_completion_set_text_column(completion,0);

  return completion;
}


void repositories_changed_callback (GtkWidget *repositories_changed,
                                    gpointer user_data)
{
  gtk_widget_destroy(GTK_WIDGET(repositories_changed));
  g_signal_emit_by_name(lookup_widget(gslapt,"action_bar_update_button"),
                        "clicked");
}


void update_activate (GtkMenuItem *menuitem, gpointer user_data)
{
  return update_callback(NULL,NULL);
}

void mark_all_upgrades_activate (GtkMenuItem *menuitem, gpointer user_data)
{
  return upgrade_callback(NULL,NULL);
}

void execute_activate (GtkMenuItem *menuitem, gpointer user_data)
{
  return execute_callback(NULL,NULL);
}

slapt_source_list_t *parse_disabled_package_sources (const char *file_name)
{
  FILE *rc = NULL;
  char *getline_buffer = NULL;
  size_t gb_length = 0;
  ssize_t g_size;
  slapt_source_list_t *list = slapt_init_source_list();

  rc = slapt_open_file(file_name,"r");
  if (rc == NULL)
    return list;

  while ( (g_size = getline(&getline_buffer,&gb_length,rc) ) != EOF ) {
    char *nl = NULL;
    if ((nl = strchr(getline_buffer,'\n')) != NULL) {
      nl[0] = '\0';
    }

    if (strstr(getline_buffer,"#DISABLED=") != NULL) {
      if (g_size > 10) {
        slapt_source_t *src = slapt_init_source(getline_buffer + 10);
        if (src != NULL)
          slapt_add_source(list,src);
      }
    }
  }
  if (getline_buffer)
    free(getline_buffer);

  fclose(rc);

  return list;
}

static gboolean toggle_source_status (GtkTreeView *treeview, gpointer data)
{
  GtkMenu *menu;
  GdkEventButton *event = (GdkEventButton *)gtk_get_current_event();
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
  GtkTreePath *path;
  GtkTreeModel *model;
  GtkTreeIter iter;

  if (event->type != GDK_BUTTON_PRESS)
    return FALSE;

  if (!gtk_tree_view_get_path_at_pos(treeview,event->x,event->y,&path,&column,NULL,NULL))
    return FALSE;

  if (strcmp(column->title,(gchar *)_("Enabled")) != 0)
    return FALSE;

  gtk_tree_path_free(path);

  if ( gtk_tree_selection_get_selected(selection,&model,&iter)) {
    gchar *source;
    gboolean status;

    gtk_tree_model_get(model,&iter,1,&source,2,&status,-1 );

    if (status) { /* is active */
      GdkPixbuf *status_icon = create_pixbuf("pkg_action_available.png");
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,
                         0,status_icon,
                         2,FALSE,
                         -1);
      gdk_pixbuf_unref(status_icon);
    } else { /* is not active */
      GdkPixbuf *status_icon = create_pixbuf("pkg_action_installed.png");
      gtk_list_store_set(GTK_LIST_STORE(model),&iter,
                         0,status_icon,
                         2,TRUE,
                         -1);
      gdk_pixbuf_unref(status_icon);
    }

    sources_modified = TRUE;
    g_free(source);
  }

  return FALSE;
}

static void display_dep_error_dialog (slapt_pkg_info_t *pkg,guint m, guint c)
{
  GtkWidget *w = create_dep_error_dialog();
  GtkTextBuffer *error_buf = NULL;
  guint i;
  gchar *msg = g_strdup_printf((gchar *)_("<b>Excluding %s due to dependency failure</b>"),pkg->name);

  gtk_window_set_title (GTK_WINDOW(w),(char *)_("Error"));
  gtk_label_set_text(GTK_LABEL(lookup_widget(w,"dep_error_label")),msg);
  g_free(msg);

  gtk_label_set_text(GTK_LABEL(lookup_widget(w,"dep_error_install_anyway_warning_label")),
                     (char *)_("Missing dependencies may mean the software in this package will not function correctly.  Do you want to continue without the required packages?"));

  gtk_label_set_use_markup (GTK_LABEL(lookup_widget(w,"dep_error_label")),
                            TRUE);
  gtk_label_set_use_markup (GTK_LABEL(lookup_widget(w,"dep_error_install_anyway_warning_label")),
                            TRUE);
  error_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(w,"dep_error_text")));

  if (m == trans->missing_err->err_count)
    m = 0;

  for (i = m; i < trans->missing_err->err_count; ++i) {
    unsigned int len = strlen(trans->missing_err->errs[i]->pkg) +
                              strlen((gchar *)_(": Depends: ")) +
                              strlen(trans->missing_err->errs[i]->error) + 2;
    char *err = slapt_malloc(sizeof *err * len);
    snprintf(err,len,"%s: Depends: %s\n",trans->missing_err->errs[i]->pkg,
             trans->missing_err->errs[i]->error);
    gtk_text_buffer_insert_at_cursor(error_buf,err,-1);
    free(err);
  }

  if (c == trans->conflict_err->err_count)
    c = 0;

  for (i = c; i < trans->conflict_err->err_count; ++i) {
    unsigned int len = strlen(trans->conflict_err->errs[i]->error) +
                              strlen((gchar *)_(", which is required by ")) +
                              strlen(trans->conflict_err->errs[i]->pkg) +
                              strlen((gchar *)_(", is excluded")) + 2;
    char *err = slapt_malloc(sizeof *err * len);
    snprintf(err,len,"%s%s%s%s\n",
             trans->conflict_err->errs[i]->error,
             (gchar *)_(", which is required by "),
             trans->conflict_err->errs[i]->pkg,
             (gchar *)_(", is excluded"));
    gtk_text_buffer_insert_at_cursor(error_buf,err,-1);
    free(err);
  }

  g_signal_connect(G_OBJECT(lookup_widget(w,"dep_error_cancel_button")),"clicked",
                   G_CALLBACK(exclude_dep_error_callback),pkg);
  g_signal_connect(G_OBJECT(lookup_widget(w,"dep_error_install_button")),"clicked",
                   G_CALLBACK(install_dep_error_callback),pkg);

  gtk_widget_show(w);
}

static void exclude_dep_error_callback (GtkObject *object, gpointer user_data)
{
  GtkWidget *dep_error_dialog = lookup_widget(GTK_WIDGET(object),"dep_error_dialog");
  slapt_pkg_info_t *pkg = (slapt_pkg_info_t *)user_data;

  slapt_add_exclude_to_transaction(trans,pkg);
  rebuild_package_action_menu();
  gtk_widget_destroy(dep_error_dialog);
}

static void install_dep_error_callback (GtkObject *object, gpointer user_data)
{
  GtkTreeView *treeview;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeModelFilter *filter_model;
  GtkTreeModelSort *package_model;
  GtkWidget *dep_error_dialog = lookup_widget(GTK_WIDGET(object),"dep_error_dialog");
  slapt_pkg_info_t *pkg = (slapt_pkg_info_t *)user_data;
  slapt_pkg_info_t *installed_pkg = slapt_get_newest_pkg(installed,pkg->name);

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));
  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  if (!set_iter_to_pkg(model,&iter,pkg))
    set_iter_to_pkg(model,&iter,installed_pkg);

  if (installed_pkg == NULL) {

    slapt_add_install_to_transaction(trans,pkg);
    set_iter_for_install(model, &iter, pkg);
    set_execute_active();

  } else {
    int ver_cmp = slapt_cmp_pkgs(installed_pkg,pkg);

    slapt_add_upgrade_to_transaction(trans,installed_pkg,pkg);

    if (ver_cmp == 0) {
      set_iter_for_reinstall(model, &iter, pkg);
    } else if (ver_cmp > 1) {
      set_iter_for_upgrade(model, &iter, pkg);
    } else if (ver_cmp < 1) {
      set_iter_for_downgrade(model, &iter, pkg);
    }

    set_execute_active();

  }

  rebuild_package_action_menu();
  gtk_widget_destroy(dep_error_dialog);
}

void view_all_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeIter iter;
  gboolean valid;
  GtkTreeModelSort *package_model;

  if (pattern && strlen(pattern) > 0)
    return build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
  
  valid = gtk_tree_model_get_iter_first(base_model,&iter);
  while (valid) {
    gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    valid = gtk_tree_model_iter_next(base_model,&iter);
  }

}

void view_available_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  gboolean show_installed = FALSE, show_available = TRUE;
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));

  view_installed_or_available_packages(show_installed, show_available);

  if (pattern && strlen(pattern) > 0)
    build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_installed_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  gboolean show_installed = TRUE, show_available = FALSE;
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));
  GtkTreeView *treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));

  view_installed_or_available_packages(show_installed, show_available);

  if (pattern && strlen(pattern) > 0)
    build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_installed_or_available_packages (gboolean show_installed, gboolean show_available)
{
  gboolean valid;
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  valid = gtk_tree_model_get_iter_first(base_model,&iter);
  while (valid) {
    gboolean from_installed_set = FALSE;

    gtk_tree_model_get(base_model,&iter,
      INST_COLUMN, &from_installed_set,
      -1
    );

    if (from_installed_set && show_installed) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else if (!from_installed_set && show_available) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,FALSE,-1);
    }

    valid = gtk_tree_model_iter_next(base_model,&iter);
  }

}


void view_marked_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  gboolean valid;
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  valid = gtk_tree_model_get_iter_first(base_model,&iter);
  while (valid) {
    gboolean marked = FALSE;

    gtk_tree_model_get(base_model,&iter,
      MARKED_COLUMN, &marked,
      -1
    );

    if (marked) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,FALSE,-1);
    }

    valid = gtk_tree_model_iter_next(base_model,&iter);
  }

  if (pattern && strlen(pattern) > 0)
    build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

static int set_iter_for_install(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
{
  gchar *status = g_strdup_printf("i%s",pkg->name);
  GdkPixbuf *status_icon = create_pixbuf("pkg_action_install.png");
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_ICON_COLUMN,status_icon,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_COLUMN,status,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,MARKED_COLUMN,TRUE,-1);
  g_free(status);
  gdk_pixbuf_unref(status_icon);
}

static int set_iter_for_reinstall(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
{
  gchar *status = g_strdup_printf("u%s",pkg->name);
  GdkPixbuf *status_icon = create_pixbuf("pkg_action_reinstall.png");
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_ICON_COLUMN,status_icon,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_COLUMN,status,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,MARKED_COLUMN,TRUE,-1);
  g_free(status);
  gdk_pixbuf_unref(status_icon);
}

static int set_iter_for_downgrade(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
{
  gchar *status = g_strdup_printf("u%s",pkg->name);
  GdkPixbuf *status_icon = create_pixbuf("pkg_action_downgrade.png");
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_ICON_COLUMN,status_icon,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_COLUMN,status,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,MARKED_COLUMN,TRUE,-1);
  g_free(status);
  gdk_pixbuf_unref(status_icon);
}

static int set_iter_for_upgrade(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
{
  gchar *status = g_strdup_printf("u%s",pkg->name);
  GdkPixbuf *status_icon = create_pixbuf("pkg_action_upgrade.png");
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_ICON_COLUMN,status_icon,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_COLUMN,status,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,MARKED_COLUMN,TRUE,-1);
  g_free(status);
  gdk_pixbuf_unref(status_icon);
}

static int set_iter_for_remove(GtkTreeModel *model, GtkTreeIter *iter,
                           slapt_pkg_info_t *pkg)
{
  gchar *status = g_strdup_printf("r%s",pkg->name);
  GdkPixbuf *status_icon = create_pixbuf("pkg_action_remove.png");
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_ICON_COLUMN,status_icon,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,STATUS_COLUMN,status,-1);
  gtk_list_store_set(GTK_LIST_STORE(model),iter,MARKED_COLUMN,TRUE,-1);
  g_free(status);
  gdk_pixbuf_unref(status_icon);
}


void mark_obsolete_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;

  set_busy_cursor();

  slapt_pkg_list_t *obsolete = slapt_get_obsolete_pkgs(
    global_config, all, installed);
  guint i;

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
  
  for (i = 0; i < obsolete->pkg_count; ++i) {

    if (slapt_is_excluded(global_config, obsolete->pkgs[i]) == 1) {

      slapt_add_exclude_to_transaction(trans, obsolete->pkgs[i]);

    } else {

      slapt_add_remove_to_transaction(trans, obsolete->pkgs[i]);
      set_iter_to_pkg(base_model, &iter, obsolete->pkgs[i]);
      set_iter_for_remove(base_model, &iter, obsolete->pkgs[i]);
      set_execute_active();

    }

  }

  unset_busy_cursor();
  slapt_free_pkg_list(obsolete);
}

static void set_busy_cursor (void)
{
  GdkCursor *c = gdk_cursor_new(GDK_WATCH);
  gdk_window_set_cursor(gslapt->window,c);
  gdk_cursor_destroy(c);
  gdk_flush();
}

static void unset_busy_cursor (void)
{
  gdk_window_set_cursor(gslapt->window,NULL);
  gdk_flush();
}


static void build_verification_sources_treeviewlist (GtkWidget *treeview)
{
  GtkListStore *store;
  GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection *select;
  guint i = 0;

  store = gtk_list_store_new (
    1,
    G_TYPE_STRING
  );

  for (i = 0; i < global_config->sources->count; ++i) {
    if ( global_config->sources->src[i]->url == NULL )
      continue;

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store,&iter, 0,global_config->sources->src[i]->url, -1);
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

#ifdef SLAPT_HAS_GPGME
static void get_gpg_key(GtkWidget *w)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *source_tree = GTK_TREE_VIEW(lookup_widget(w,"preferences_verification_sources_treeview"));
  GtkTreeSelection *select = gtk_tree_view_get_selection (GTK_TREE_VIEW (source_tree));
  GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(source_tree));

  if ( gtk_tree_selection_get_selected(select,&model,&iter)) {
    GtkLabel *progress_action_label, *progress_message_label;
    GtkProgressBar *p_bar, *dl_bar;
    unsigned int compressed = 0;
    FILE *gpg_key= NULL;
    slapt_code_t result;
    gchar *url;
    gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &url, -1);

    progress_window = create_dl_progress_window();
    gtk_window_set_title(GTK_WINDOW(progress_window),(gchar *)_("Progress"));
    p_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"progress_progressbar"));
    dl_bar = GTK_PROGRESS_BAR(lookup_widget(progress_window,"dl_progress"));
    progress_action_label = GTK_LABEL(lookup_widget(progress_window,"progress_action"));
    progress_message_label = GTK_LABEL(lookup_widget(progress_window,"progress_message"));
    gtk_progress_bar_set_fraction(dl_bar,0.0);
    gtk_label_set_text(progress_message_label,url);
    gtk_label_set_text(progress_action_label,(gchar *)SLAPT_GPG_KEY);

    gdk_threads_enter();
    gtk_widget_show(progress_window);
    gdk_threads_leave();

    gpg_key = slapt_get_pkg_source_gpg_key(global_config, url, &compressed);

    if (_cancelled == 1)
    {
      G_LOCK(_cancelled);
      _cancelled = 0;
      G_UNLOCK(_cancelled);
      if (gpg_key) {
        fclose(gpg_key);
        gpg_key = NULL;
      }
    }

    if (gpg_key)
    {
      result = slapt_add_pkg_source_gpg_key (gpg_key);
      gdk_threads_enter();
      notify(_("Import"),slapt_strerror(result));
      gdk_threads_leave();
    } else {
      gdk_threads_enter();
      notify(_("Import"),_("No key found"));
      gdk_threads_leave();
    }

    g_free(url);

    gdk_threads_enter();
    gtk_widget_destroy(progress_window);
    gdk_threads_leave();

  }

}

void preferences_sources_add_key (GtkWidget *w, gpointer user_data)
{
  GThread *gdp;
  gdp = g_thread_create((GThreadFunc)get_gpg_key,w,FALSE,NULL);

  return;
}
#endif

void view_upgradeable_packages (GtkMenuItem *menuitem, gpointer user_data)
{
  gboolean valid;
  GtkTreeIter iter;
  GtkTreeModelFilter *filter_model;
  GtkTreeModel *base_model;
  GtkTreeModelSort *package_model;
  GtkTreeView *treeview;
  gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_entry")));

  treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"pkg_listing_treeview"));
  package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

  filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
  base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

  valid = gtk_tree_model_get_iter_first(base_model,&iter);
  while (valid) {
    gboolean upgradeable = FALSE;

    gtk_tree_model_get(base_model,&iter,
      UPGRADEABLE_COLUMN, &upgradeable,
      -1
    );

    if (upgradeable) {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,TRUE,-1);
    } else {
      gtk_list_store_set(GTK_LIST_STORE(base_model),&iter,VISIBLE_COLUMN,FALSE,-1);
    }

    valid = gtk_tree_model_iter_next(base_model,&iter);
  }

  if (pattern && strlen(pattern) > 0)
    build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_changelogs (GtkMenuItem *menuitem, gpointer user_data)
{
  int i, changelogs = 0;
  GtkWidget *changelog_window = create_changelog_window();
  GtkWidget *changelog_notebook = lookup_widget(changelog_window, "changelog_notebook");

  if ((gslapt_settings->cl_x == gslapt_settings->cl_y == gslapt_settings->cl_width == gslapt_settings->cl_height == 0)) {
    gtk_window_set_default_size(GTK_WINDOW(changelog_window),
      gslapt_settings->cl_width, gslapt_settings->cl_height);
    gtk_window_move(GTK_WINDOW(changelog_window),
      gslapt_settings->cl_x, gslapt_settings->cl_y);
  }

  for (i = 0; i < global_config->sources->count; ++i) {
    char *changelog_filename, *changelog_data;
    gchar *source_url, *path_and_file, *changelog_txt;
    struct stat stat_buf;
    size_t pls = 1;
    FILE *changelog_f = NULL;
    GtkWidget *textview, *scrolledwindow, *label;
    GtkTextBuffer *changelog_buffer;

    if ( global_config->sources->src[i]->url == NULL )
      continue;
    if ( global_config->sources->src[i]->disabled == SLAPT_TRUE)
      continue;

    source_url = g_strdup ( global_config->sources->src[i]->url );

    changelog_filename = slapt_gen_filename_from_url(source_url,SLAPT_CHANGELOG_FILE);
    path_and_file = g_strjoin("/", global_config->working_dir, changelog_filename, NULL);

    if ((changelog_f = fopen(path_and_file,"rb")) == NULL) {
      free(changelog_filename);
      g_free(path_and_file);
      g_free(source_url);
      continue;
    }

    if (stat(changelog_filename,&stat_buf) == -1) {
      fclose(changelog_f);
      free(changelog_filename);
      g_free(path_and_file);
      g_free(source_url);
      continue;
    }

    free(changelog_filename);
    g_free(path_and_file);

    /* don't mmap empty files */
    if ((int)stat_buf.st_size < 1) {
      fclose(changelog_f);
      g_free(source_url);
      continue;
    }

    pls = (size_t)stat_buf.st_size;

    changelog_data = (char *)mmap(0, pls,
      PROT_READ|PROT_WRITE, MAP_PRIVATE, fileno(changelog_f), 0);

    fclose(changelog_f);

    if (changelog_data == (void *)-1) {
      g_free(source_url);
      continue;
    }

    changelog_data[pls - 1] = '\0';

    changelog_txt = g_strdup(changelog_data);

    /* munmap now that we are done */
    if (munmap(changelog_data,pls) == -1) {
      g_free(changelog_txt);
      g_free(source_url);
      continue;
    }

    scrolledwindow  = gtk_scrolled_window_new ( NULL, NULL );
    textview        = gtk_text_view_new ();
    label           = gtk_label_new ( source_url );

    if (!g_utf8_validate(changelog_txt, -1, NULL)) {
      gchar *converted = g_convert(changelog_txt, strlen(changelog_txt), "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
      if (converted != NULL) {
        g_free(changelog_txt);
        changelog_txt = converted;
      }
    }

    changelog_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(changelog_buffer, changelog_txt, -1);
    gtk_text_view_set_editable (GTK_TEXT_VIEW (textview), FALSE);

    gtk_widget_show( scrolledwindow );
    gtk_widget_show( textview );
    gtk_widget_show( label );

    gtk_container_add ( GTK_CONTAINER(changelog_notebook), scrolledwindow );
    gtk_container_set_border_width ( GTK_CONTAINER(scrolledwindow), 2 );
    gtk_scrolled_window_set_policy ( GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
    
    gtk_container_add ( GTK_CONTAINER(scrolledwindow), textview );
    gtk_notebook_set_tab_label (GTK_NOTEBOOK (changelog_notebook), gtk_notebook_get_nth_page (GTK_NOTEBOOK (changelog_notebook), changelogs), label);

    g_free(changelog_txt);
    g_free(source_url);
    changelogs++;
  }

  if (changelogs > 0) {
    gtk_widget_show(changelog_window);
  } else {
    gtk_widget_destroy(changelog_window);
    notify((gchar *)_("ChangeLogs"),_("No changelogs found."));
  }
}

void cancel_source_edit (GtkWidget *w, gpointer user_data)
{
  gtk_widget_destroy(w);
}

void source_edit_ok (GtkWidget *w, gpointer user_data)
{
  SLAPT_PRIORITY_T priority;
  const char *original_url      = NULL;
  const gchar *source           = NULL;
  GtkEntry  *source_entry       = GTK_ENTRY(lookup_widget(w,"source_entry"));
  GtkComboBox *source_priority  = GTK_COMBO_BOX(lookup_widget(w,"source_priority"));
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeView *source_tree      = GTK_TREE_VIEW(lookup_widget(preferences_window,"preferences_sources_treeview"));
  GtkTreeSelection *select      = gtk_tree_view_get_selection (GTK_TREE_VIEW (source_tree));
  GtkListStore *store           = GTK_LIST_STORE(gtk_tree_view_get_model(source_tree));

  source  = gtk_entry_get_text(source_entry);

  if ( source == NULL || strlen(source) < 1 )
    return;

  priority = convert_gslapt_priority_to_slapt_priority(gtk_combo_box_get_active(source_priority));

  if ((original_url = g_object_get_data( G_OBJECT(w), "original_url")) != NULL) {
    const char *priority_str = slapt_priority_to_str(priority);

    if ( gtk_tree_selection_get_selected(select,&model,&iter))
      gtk_list_store_set(store, &iter, 1, source, 3, priority_str, 4, priority, -1);

  } else {
    const char *priority_str = slapt_priority_to_str(priority);
    GdkPixbuf *status_icon   = create_pixbuf("pkg_action_installed.png");
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
      0, status_icon,
      1, source,
      2, TRUE,
      3, priority_str,
      4, priority,
      -1);
    gdk_pixbuf_unref(status_icon);
  }

  sources_modified = TRUE;
  gtk_widget_destroy(w);
}

static SLAPT_PRIORITY_T convert_gslapt_priority_to_slapt_priority(gint p)
{
  switch (p) {
    case 1:
      return SLAPT_PRIORITY_OFFICIAL;
    case 2:
      return SLAPT_PRIORITY_PREFERRED;
    case 3:
      return SLAPT_PRIORITY_CUSTOM;
    case 0:
    default:
      return SLAPT_PRIORITY_DEFAULT;
  };
}

static gint convert_slapt_priority_to_gslapt_priority(SLAPT_PRIORITY_T p)
{
  switch (p) {
    case SLAPT_PRIORITY_DEFAULT:
      return 0;
    case SLAPT_PRIORITY_OFFICIAL:
      return 1;
    case SLAPT_PRIORITY_PREFERRED:
      return 2;
    case SLAPT_PRIORITY_CUSTOM:
      return 3;
    default:
      return -1;
  };
}

