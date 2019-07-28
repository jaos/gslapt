/*
 * Copyright (C) 2003-2019 Jason Woodward <woodwardj at jaos dot org>
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
#include <config.h>
#endif

#define _GNU_SOURCE

#include <gtk/gtk.h>

#include "callbacks.h"
#include "settings.h"
#include "series.h"

extern GtkWidget *gslapt;
extern GtkBuilder *gslapt_builder;
extern GslaptSettings *gslapt_settings;
extern GHashTable *gslapt_series_map;
extern slapt_config_t *global_config;
extern slapt_vector_t *all;
extern slapt_vector_t *installed;
extern slapt_transaction_t *trans;
extern char rc_location[];

G_LOCK_DEFINE_STATIC(_cancelled);
static volatile guint _cancelled = 0;
static gboolean sources_modified = FALSE;
static gboolean excludes_modified = FALSE;

static gboolean pkg_action_popup_menu(GtkTreeView *treeview, gpointer data);
static int set_iter_to_pkg(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);
static slapt_pkg_upgrade_t *lsearch_upgrade_transaction(slapt_transaction_t *tran, slapt_pkg_t *pkg);
static void build_package_action_menu(slapt_pkg_t *pkg);
static void rebuild_package_action_menu(void);
static void mark_upgrade_packages(void);
static void fillin_pkg_details(slapt_pkg_t *pkg);
static void get_package_data(void);
static void rebuild_treeviews(GtkWidget *current_window, gboolean reload);
static void gslapt_set_status(const gchar *);
static void gslapt_clear_status(void);
static void lock_toolbar_buttons(void);
static void unlock_toolbar_buttons(void);
static void build_sources_treeviewlist(GtkWidget *treeview);
#ifdef SLAPT_HAS_GPGME
static void build_verification_sources_treeviewlist(GtkWidget *treeview);
#endif
static void build_exclude_treeviewlist(GtkWidget *treeview);
static int populate_transaction_window(void);
char *download_packages(void);
static gboolean install_packages(void);
static void set_execute_active(void);
static void clear_execute_active(void);
static void notify(const char *title, const char *message);
static void reset_search_list(void);
static int ladd_deps_to_trans(slapt_transaction_t *tran, slapt_vector_t *avail_pkgs, slapt_vector_t *installed_pkgs, slapt_pkg_t *pkg);
static gboolean toggle_source_status(GtkTreeView *treeview, gpointer data);
static void display_dep_error_dialog(slapt_pkg_t *pkg);
static void view_installed_or_available_packages(gboolean show_installed, gboolean show_available);

static int set_iter_for_install(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);
static int set_iter_for_reinstall(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);
static int set_iter_for_downgrade(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);
static int set_iter_for_upgrade(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);
static int set_iter_for_remove(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg);

static void set_busy_cursor(void);
static void unset_busy_cursor(void);
static SLAPT_PRIORITY_T convert_gslapt_priority_to_slapt_priority(gint p);
static gint convert_slapt_priority_to_gslapt_priority(SLAPT_PRIORITY_T p);

gboolean gslapt_window_resized(GtkWindow *window, GdkEvent *event, gpointer data)
{
    const char *widget_name = gtk_buildable_get_name(GTK_BUILDABLE(window));

    gint x = event->configure.x;
    gint y = event->configure.y;
    gint width = event->configure.width;
    gint height = event->configure.height;

    if (strcmp(widget_name, "gslapt") == 0) {
        gslapt_settings->x = x;
        gslapt_settings->y = y;
        gslapt_settings->width = width;
        gslapt_settings->height = height;
    } else if (strcmp(widget_name, "window_preferences") == 0) {
        gslapt_settings->pref_x = x;
        gslapt_settings->pref_y = y;
        gslapt_settings->pref_width = width;
        gslapt_settings->pref_height = height;
    } else if (strcmp(widget_name, "changelog_window") == 0) {
        gslapt_settings->cl_x = x;
        gslapt_settings->cl_y = y;
        gslapt_settings->cl_width = width;
        gslapt_settings->cl_height = height;
    } else if (strcmp(widget_name, "transaction_window") == 0) {
        gslapt_settings->tran_x = x;
        gslapt_settings->tran_y = y;
        gslapt_settings->tran_width = width;
        gslapt_settings->tran_height = height;
    } else if (strcmp(widget_name, "dl_progress_window") == 0 || strcmp(widget_name, "pkgtools_progress_window") == 0) {
        gslapt_settings->progress_x = x;
        gslapt_settings->progress_y = y;
        gslapt_settings->progress_width = width;
        gslapt_settings->progress_height = height;
    } else if (strcmp(widget_name, "notification") == 0 ) {
        gslapt_settings->notify_x = x;
        gslapt_settings->notify_y = y;
        gslapt_settings->notify_width = width;
        gslapt_settings->notify_height = height;
    } else {
        fprintf(stderr, "need to handle widget name: %s\n", widget_name);
    }

    return FALSE;
}

void on_gslapt_destroy(GObject *object, gpointer user_data)
{
    slapt_free_transaction(trans);
    slapt_vector_t_free(all);
    slapt_vector_t_free(installed);
    slapt_config_t_free(global_config);
    gslapt_series_map_free(gslapt_series_map);

    gslapt_write_rc(gslapt_settings);
    gslapt_free_rc(gslapt_settings);

    gtk_main_quit();
    exit(0);
}

void update_callback(GObject *object, gpointer user_data)
{
    clear_execute_active();
    set_busy_cursor();

#if !GLIB_CHECK_VERSION(2, 31, 0)
    GThread *gdp = g_thread_create((GThreadFunc)get_package_data, NULL, FALSE, NULL);
#else
    GThread *gdp = g_thread_new("GslaptUpdateCallback", (GThreadFunc)get_package_data, NULL);
    g_thread_unref(gdp);
#endif

    return;
}

void upgrade_callback(GObject *object, gpointer user_data)
{
    set_busy_cursor();
    mark_upgrade_packages();
    if (trans->install_pkgs->size > 0 || trans->upgrade_pkgs->size > 0 || trans->reinstall_pkgs->size > 0) {
        set_execute_active();
    }
    unset_busy_cursor();
}

void execute_callback(GObject *object, gpointer user_data)
{
    if ((trans->install_pkgs->size == 0) && (trans->upgrade_pkgs->size == 0) && (trans->reinstall_pkgs->size == 0) && (trans->remove_pkgs->size == 0))
        return;

    gslapt_load_ui(gslapt_builder, "transaction_window.ui");
    GtkWidget *trans_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "transaction_window"));
    gtk_window_set_transient_for(GTK_WINDOW(trans_window), GTK_WINDOW(gslapt));

    if(!(gslapt_settings->tran_width == 0 && gslapt_settings->tran_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(trans_window), gslapt_settings->tran_width, gslapt_settings->tran_height);
    if (!(gslapt_settings->tran_x == 0 && gslapt_settings->tran_y == 0))
        gtk_window_move(GTK_WINDOW(trans_window), gslapt_settings->tran_x, gslapt_settings->tran_y);

    if (populate_transaction_window() == 0) {
        gtk_widget_show_all(trans_window);
    } else {
        gtk_widget_destroy(trans_window);
    }
    set_busy_cursor();
}

static GtkWidget *preferences_window;
void open_preferences(GtkMenuItem *menuitem, gpointer user_data)
{
#ifdef SLAPT_HAS_GPGME
    GtkTreeView *verification_source_tree;
#endif

    gslapt_load_ui(gslapt_builder, "window_preferences.ui");
    preferences_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "window_preferences"));
    gtk_window_set_transient_for(GTK_WINDOW(preferences_window), GTK_WINDOW(gslapt));

    if(!(gslapt_settings->pref_width == 0 && gslapt_settings->pref_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(preferences_window), gslapt_settings->pref_width, gslapt_settings->pref_height);
    if (!(gslapt_settings->pref_x == 0 && gslapt_settings->pref_y == 0))
        gtk_window_move(GTK_WINDOW(preferences_window), gslapt_settings->pref_x, gslapt_settings->pref_y);

    GtkEntry *working_dir = GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "preferences_working_dir_entry"));
    gtk_entry_set_text(working_dir, global_config->working_dir);

    GtkTreeView *source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_sources_treeview"));
    build_sources_treeviewlist((GtkWidget *)source_tree);

#ifdef SLAPT_HAS_GPGME
    verification_source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_verification_sources_treeview"));
    build_verification_sources_treeviewlist((GtkWidget *)verification_source_tree);
#endif

    GtkTreeView *exclude_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_exclude_treeview"));
    build_exclude_treeviewlist((GtkWidget *)exclude_tree);

    gtk_widget_show_all(preferences_window);
}

void search_activated(GtkWidget *gslapt, gpointer user_data)
{
    gboolean valid = FALSE, exists = FALSE;
    GtkTreeIter iter;
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));
    GtkEntryCompletion *completion = gtk_entry_get_completion(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));
    GtkTreeModel *completions = gtk_entry_completion_get_model(completion);

    build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);

    /* add search to completion */
    valid = gtk_tree_model_get_iter_first(completions, &iter);
    while (valid) {
        gchar *string = NULL;
        gtk_tree_model_get(completions, &iter, 0, &string, -1);
        if (strcmp(string, pattern) == 0)
            exists = TRUE;
        g_free(string);
        valid = gtk_tree_model_iter_next(completions, &iter);
    }
    if (!exists) {
        gtk_list_store_append(GTK_LIST_STORE(completions), &iter);
        gtk_list_store_set(GTK_LIST_STORE(completions), &iter, 0, pattern, -1);
    }
}

void add_pkg_for_install(GtkWidget *gslapt, gpointer user_data)
{
    slapt_pkg_t *pkg = NULL, *installed_pkg = NULL;
    GtkTreeView *treeview;
    GtkTreeIter iter;
    GtkTreeSelection *selection;
    GtkBin *caller_button = (GtkBin *)user_data;
    GtkLabel *caller_button_label = GTK_LABEL(gtk_bin_get_child(caller_button));
    GtkTreeModel *model;
    GtkTreeIter actual_iter, filter_iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModelSort *package_model;

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    selection = gtk_tree_view_get_selection(treeview);
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    if (gtk_tree_selection_get_selected(selection, (GtkTreeModel **)&package_model, &iter) == TRUE) {
        gchar *pkg_name;
        gchar *pkg_version;
        gchar *pkg_location;

        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

        if (pkg_name == NULL || pkg_version == NULL || pkg_location == NULL) {
            fprintf(stderr, "failed to get package name and version from selection\n");

            if (pkg_name != NULL)
                g_free(pkg_name);

            if (pkg_version != NULL)
                g_free(pkg_version);

            if (pkg_location != NULL)
                g_free(pkg_location);

            return;
        }

        if (strcmp(gtk_label_get_text(caller_button_label), (gchar *)_("Upgrade")) == 0) {
            pkg = slapt_get_newest_pkg(all, pkg_name);
        } else if (strcmp(gtk_label_get_text(caller_button_label), (gchar *)_("Re-Install")) == 0) {
            pkg = slapt_get_exact_pkg(all, pkg_name, pkg_version);
        } else {
            pkg = slapt_get_pkg_by_details(all, pkg_name, pkg_version, pkg_location);
        }

        if (pkg == NULL) {
            fprintf(stderr, "Failed to find package: %s-%s@%s\n", pkg_name, pkg_version, pkg_location);
            g_free(pkg_name);
            g_free(pkg_version);
            g_free(pkg_location);
            return;
        }

        installed_pkg = slapt_get_newest_pkg(installed, pkg_name);

        g_free(pkg_name);
        g_free(pkg_version);
        g_free(pkg_location);
    }

    if (pkg == NULL) {
        fprintf(stderr, "No package to work with\n");
        return;
    }

    /* convert sort model and iter to filter */
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model), &filter_iter, &iter);
    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    /* convert filter to regular tree */
    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model), &actual_iter, &filter_iter);
    model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    /* a=installed,i=install,r=remove,u=upgrade,z=available */

    /* if it is not already installed, install it */
    if (installed_pkg == NULL) {
        if (ladd_deps_to_trans(trans, all, installed, pkg) == 0) {
            slapt_vector_t *conflicts = slapt_is_conflicted(trans, all, installed, pkg);

            slapt_add_install_to_transaction(trans, pkg);
            set_iter_for_install(model, &actual_iter, pkg);
            set_execute_active();

            /* if there is a conflict, we schedule the conflict for removal */
            if (conflicts->size > 0) {
                slapt_vector_t_foreach (slapt_pkg_t *, conflicted_pkg, conflicts) {
                    slapt_add_remove_to_transaction(trans, conflicted_pkg);
                    set_execute_active();
                    if (set_iter_to_pkg(model, &actual_iter, conflicted_pkg)) {
                        set_iter_for_remove(model, &actual_iter, conflicted_pkg);
                    }
                }
            }
            slapt_vector_t_free(conflicts);
        } else {
            display_dep_error_dialog(pkg);
        }

    } else { /* else we upgrade or reinstall */
        int ver_cmp;

        /* it is already installed, attempt an upgrade */
        if (((ver_cmp = slapt_cmp_pkgs(installed_pkg, pkg)) < 0) || (global_config->re_install == TRUE)) {
            if (ladd_deps_to_trans(trans, all, installed, pkg) == 0) {
                slapt_vector_t *conflicts = slapt_is_conflicted(trans, all, installed, pkg);

                if (conflicts->size > 0) {
                    slapt_vector_t_foreach (slapt_pkg_t *, conflicted_pkg, conflicts) {
                        fprintf(stderr, "%s conflicts with %s\n", pkg->name, conflicted_pkg->name);
                        slapt_add_remove_to_transaction(trans, conflicted_pkg);
                        set_execute_active();
                        if (set_iter_to_pkg(model, &actual_iter, conflicted_pkg)) {
                            set_iter_for_remove(model, &actual_iter, conflicted_pkg);
                        }
                    }
                } else {
                    if (global_config->re_install == TRUE)
                        slapt_add_reinstall_to_transaction(trans, installed_pkg, pkg);
                    else
                        slapt_add_upgrade_to_transaction(trans, installed_pkg, pkg);

                    if (global_config->re_install == TRUE) {
                        if (ver_cmp == 0) {
                            set_iter_for_reinstall(model, &actual_iter, pkg);
                        } else {
                            set_iter_for_downgrade(model, &actual_iter, pkg);
                            if (set_iter_to_pkg(model, &actual_iter, installed_pkg)) {
                                set_iter_for_downgrade(model, &actual_iter, installed_pkg);
                            }
                        }
                    } else {
                        slapt_pkg_t *inst_avail = slapt_get_exact_pkg(all, installed_pkg->name, installed_pkg->version);
                        set_iter_for_upgrade(model, &actual_iter, pkg);
                        if (pkg != NULL && set_iter_to_pkg(model, &actual_iter, pkg)) {
                            set_iter_for_upgrade(model, &actual_iter, pkg);
                        }
                        if (installed_pkg != NULL && set_iter_to_pkg(model, &actual_iter, installed_pkg)) {
                            set_iter_for_upgrade(model, &actual_iter, installed_pkg);
                        }
                        if (inst_avail != NULL && set_iter_to_pkg(model, &actual_iter, inst_avail)) {
                            set_iter_for_upgrade(model, &actual_iter, inst_avail);
                        }
                    }
                    set_execute_active();
                }

                slapt_vector_t_free(conflicts);
            } else {
                display_dep_error_dialog(pkg);
            }
        }
    }

    rebuild_package_action_menu();
}

void add_pkg_for_removal(GtkWidget *gslapt, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    GtkTreeModelSort *package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    if (gtk_tree_selection_get_selected(selection, (GtkTreeModel **)&package_model, &iter) == TRUE) {
        gchar *pkg_name;
        gchar *pkg_version;
        gchar *pkg_location;
        slapt_pkg_t *pkg;

        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &pkg_name, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &pkg_version, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &pkg_location, -1);

        if ((pkg = slapt_get_exact_pkg(installed, pkg_name, pkg_version)) != NULL) {
            slapt_vector_t *deps;
            GtkTreeModel *model;
            GtkTreeIter filter_iter, actual_iter;
            GtkTreeModelFilter *filter_model;

            /* convert sort model and iter to filter */
            gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model), &filter_iter, &iter);
            filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
            /* convert filter to regular tree */
            gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model), &actual_iter, &filter_iter);
            model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

            deps = slapt_is_required_by(global_config, all, installed, trans->install_pkgs, trans->remove_pkgs, pkg);

            slapt_add_remove_to_transaction(trans, pkg);
            set_iter_for_remove(model, &actual_iter, pkg);
            set_execute_active();

            slapt_vector_t_foreach (slapt_pkg_t *, dep, deps) {
                /* need to check that it is actually installed */
                slapt_pkg_t *installed_dep = slapt_get_exact_pkg(installed, dep->name, dep->version);
                if (installed_dep != NULL) {
                    slapt_add_remove_to_transaction(trans, installed_dep);
                    if (set_iter_to_pkg(model, &actual_iter, installed_dep)) {
                        set_iter_for_remove(model, &actual_iter, installed_dep);
                    }
                }
            }

            slapt_vector_t_free(deps);
        }

        g_free(pkg_name);
        g_free(pkg_version);
        g_free(pkg_location);
    }

    rebuild_package_action_menu();
}

void build_package_treeviewlist(GtkWidget *treeview)
{
    GtkTreeIter iter;
    GtkTreeModel *base_model;
    GtkTreeModelFilter *filter_model;
    GtkTreeModelSort *package_model;

    base_model = GTK_TREE_MODEL(gtk_list_store_new(
        NUMBER_OF_COLUMNS,
        GDK_TYPE_PIXBUF, /* status icon */
        G_TYPE_STRING,   /* name */
        G_TYPE_STRING,   /* version */
        G_TYPE_STRING,   /* location */
        G_TYPE_STRING,   /* series */
        G_TYPE_STRING,   /*desc */
        G_TYPE_UINT,     /* size */
        G_TYPE_STRING,   /* status */
        G_TYPE_BOOLEAN,  /* is installed */
        G_TYPE_BOOLEAN,  /* visible */
        G_TYPE_BOOLEAN,  /* marked */
        G_TYPE_BOOLEAN   /* is an upgrade */
        ));

    slapt_vector_t_foreach (slapt_pkg_t *, pkg, all) {
        /* we use this for sorting the status */
        /* a=installed,i=install,r=remove,u=upgrade,z=available */
        gchar *status = NULL;
        gboolean is_inst = FALSE, is_an_upgrade = FALSE;
        GdkPixbuf *status_icon = NULL;
        gchar *short_desc = slapt_gen_short_pkg_description(pkg);
        slapt_pkg_t *installed_pkg = NULL, *newer_available_pkg = NULL;
        gchar *location = NULL, *series = NULL;

        installed_pkg = slapt_get_newest_pkg(installed, pkg->name);
        if (installed_pkg != NULL) {
            int cmp = slapt_cmp_pkgs(pkg, installed_pkg);
            if (strcmp(pkg->version, installed_pkg->version) == 0) {
                is_inst = TRUE;
            } else if (cmp > 0) {
                is_an_upgrade = TRUE;

                /* we need to see if there is another available package that is newer than this one */
                if ((newer_available_pkg = slapt_get_newest_pkg(all, pkg->name)) != NULL) {
                    if (slapt_cmp_pkgs(pkg, newer_available_pkg) < 0)
                        is_an_upgrade = FALSE;
                }
            }
        }

        if (slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) {
            /* if it's excluded */
            if ((slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) || slapt_is_excluded(global_config, pkg) == 1) {
                status_icon = gslapt_img("pkg_action_available_excluded.png");
            } else {
                status_icon = gslapt_img("pkg_action_available.png");
            }
            status = g_strdup_printf("z%s", pkg->name);
            location = pkg->location;
        } else if (slapt_get_exact_pkg(trans->remove_pkgs, pkg->name, pkg->version) != NULL) {
            status_icon = gslapt_img("pkg_action_remove.png");
            status = g_strdup_printf("r%s", pkg->name);
            location = pkg->location;
        } else if (slapt_get_exact_pkg(trans->install_pkgs, pkg->name, pkg->version) != NULL) {
            status_icon = gslapt_img("pkg_action_install.png");
            status = g_strdup_printf("i%s", pkg->name);
            location = pkg->location;
        } else if (lsearch_upgrade_transaction(trans, pkg) != NULL) {
            status_icon = gslapt_img("pkg_action_upgrade.png");
            status = g_strdup_printf("u%s", pkg->name);
            location = pkg->location;
        } else if (is_inst) {
            /* if it's excluded */
            if ((slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) || slapt_is_excluded(global_config, pkg) == 1) {
                status_icon = gslapt_img("pkg_action_installed_excluded.png");
            } else {
                status_icon = gslapt_img("pkg_action_installed.png");
            }
            status = g_strdup_printf("a%s", pkg->name);
            location = installed_pkg->location;
        } else {
            /* if it's excluded */
            if ((slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) || slapt_is_excluded(global_config, pkg) == 1) {
                status_icon = gslapt_img("pkg_action_available_excluded.png");
            } else {
                status_icon = gslapt_img("pkg_action_available.png");
            }
            status = g_strdup_printf("z%s", pkg->name);
            location = pkg->location;
        }

        series = gslapt_series_map_lookup(gslapt_series_map, location);
        if (series == NULL)
            series = g_strdup(location);

        gtk_list_store_append(GTK_LIST_STORE(base_model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(base_model), &iter,
                           STATUS_ICON_COLUMN, status_icon,
                           NAME_COLUMN, pkg->name,
                           VERSION_COLUMN, pkg->version,
                           LOCATION_COLUMN, location,
                           SERIES_COLUMN, series,
                           DESC_COLUMN, short_desc,
                           SIZE_COLUMN, pkg->size_u,
                           STATUS_COLUMN, status,
                           INST_COLUMN, is_inst,
                           VISIBLE_COLUMN, TRUE,
                           MARKED_COLUMN, FALSE,
                           UPGRADEABLE_COLUMN, is_an_upgrade,
                           -1);

        g_free(status);
        g_free(short_desc);
        g_free(series);
        g_object_unref(status_icon);
    }

    slapt_vector_t_foreach (slapt_pkg_t *, installed_pkg, installed) {
        /* do not duplicate those packages that are still available from the package sources */
        if (slapt_get_exact_pkg(all, installed_pkg->name, installed_pkg->version) == NULL) {
            /* we use this for sorting the status */
            /* a=installed,i=install,r=remove,u=upgrade,z=available */
            gchar *status = NULL;
            GdkPixbuf *status_icon = NULL;
            gchar *short_desc = slapt_gen_short_pkg_description(installed_pkg);

            if (slapt_get_exact_pkg(trans->remove_pkgs, installed_pkg->name, installed_pkg->version) != NULL) {
                status_icon = gslapt_img("pkg_action_remove.png");
                status = g_strdup_printf("r%s", installed_pkg->name);
            } else {
                /* if it's excluded */
                if ((slapt_get_exact_pkg(trans->exclude_pkgs, installed_pkg->name, installed_pkg->version) != NULL) || slapt_is_excluded(global_config, installed_pkg) == 1) {
                    status_icon = gslapt_img("pkg_action_installed_excluded.png");
                } else {
                    status_icon = gslapt_img("pkg_action_installed.png");
                }
                status = g_strdup_printf("a%s", installed_pkg->name);
            }

            gtk_list_store_append(GTK_LIST_STORE(base_model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter,
                               STATUS_ICON_COLUMN, status_icon,
                               NAME_COLUMN, installed_pkg->name,
                               VERSION_COLUMN, installed_pkg->version,
                               LOCATION_COLUMN, installed_pkg->location,
                               SERIES_COLUMN, installed_pkg->location,
                               DESC_COLUMN, short_desc,
                               SIZE_COLUMN, installed_pkg->size_u,
                               STATUS_COLUMN, status,
                               INST_COLUMN, TRUE,
                               VISIBLE_COLUMN, TRUE,
                               MARKED_COLUMN, FALSE,
                               UPGRADEABLE_COLUMN, FALSE,
                               -1);

            g_free(status);
            g_free(short_desc);
            g_object_unref(status_icon);
        }
    }

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(base_model, NULL));
    gtk_tree_model_filter_set_visible_column(filter_model, VISIBLE_COLUMN);
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(filter_model)));
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(package_model));
    g_object_unref(package_model);

    if (gtk_widget_get_window(gslapt) != NULL) {
        unset_busy_cursor();
    }
}

void build_searched_treeviewlist(GtkWidget *treeview, gchar *pattern)
{
    gboolean valid;
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    slapt_vector_t *a_matches = NULL, *i_matches = NULL;
    GtkTreeModelSort *package_model;
    gboolean view_list_all = FALSE, view_list_installed = FALSE,
             view_list_available = FALSE, view_list_marked = FALSE,
             view_list_upgradeable = FALSE;
    slapt_regex_t *series_regex = NULL;

    if (pattern == NULL || (strcmp(pattern, "") == 0)) {
        reset_search_list();
    } else {
        series_regex = slapt_regex_t_init(pattern);
    }

    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    view_list_all = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_all_packages_menu")));
    view_list_available = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_available_packages_menu")));
    view_list_installed = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_installed_packages_menu")));
    view_list_marked = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_marked_packages_menu")));
    view_list_upgradeable = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_upgradeable_packages_menu")));

    a_matches = slapt_search_pkg_list(all, pattern);
    i_matches = slapt_search_pkg_list(installed, pattern);
    valid = gtk_tree_model_get_iter_first(base_model, &iter);
    while (valid) {
        gchar *name = NULL, *version = NULL, *location = NULL, *series = NULL;
        slapt_pkg_t *a_pkg = NULL, *i_pkg = NULL;
        gboolean marked = FALSE;
        gboolean upgradeable = FALSE;
        gboolean series_match = FALSE;

        gtk_tree_model_get(base_model, &iter,
                           NAME_COLUMN, &name,
                           VERSION_COLUMN, &version,
                           LOCATION_COLUMN, &location,
                           SERIES_COLUMN, &series,
                           MARKED_COLUMN, &marked,
                           UPGRADEABLE_COLUMN, &upgradeable,
                           -1);

        if (series_regex != NULL) {
            slapt_regex_t_execute(series_regex, series);
            if (series_regex->reg_return == 0) {
                series_match = TRUE;
            }
        }

        a_pkg = slapt_get_pkg_by_details(a_matches, name, version, location);
        i_pkg = slapt_get_pkg_by_details(i_matches, name, version, location);

        if (view_list_installed && i_pkg != NULL) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (view_list_available && a_pkg != NULL) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (view_list_all && (a_pkg != NULL || i_pkg != NULL)) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (view_list_marked && marked && (a_pkg != NULL || i_pkg != NULL)) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (view_list_upgradeable && upgradeable && a_pkg != NULL) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (series_match == TRUE) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, FALSE, -1);
        }

        g_free(name);
        g_free(version);
        g_free(location);
        g_free(series);
        valid = gtk_tree_model_iter_next(base_model, &iter);
    }

    if (series_regex != NULL)
        slapt_regex_t_free(series_regex);

    slapt_vector_t_free(a_matches);
    slapt_vector_t_free(i_matches);
}

void open_about(GObject *object, gpointer user_data)
{
    gslapt_load_ui(gslapt_builder, "about.ui");
    GtkWidget *about = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "about"));
    gtk_window_set_transient_for(GTK_WINDOW(about), GTK_WINDOW(gslapt));

    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about), VERSION);
    gtk_dialog_run(GTK_DIALOG(about));
    gtk_widget_destroy(about);
}

void show_pkg_details(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeIter iter;
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    GtkTreeModelSort *package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    if (gtk_tree_selection_get_selected(selection, (GtkTreeModel **)&package_model, &iter)) {
        gchar *p_name, *p_version, *p_location;
        slapt_pkg_t *pkg;

        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, NAME_COLUMN, &p_name, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, VERSION_COLUMN, &p_version, -1);
        gtk_tree_model_get(GTK_TREE_MODEL(package_model), &iter, LOCATION_COLUMN, &p_location, -1);

        pkg = slapt_get_pkg_by_details(all, p_name, p_version, p_location);
        if (pkg != NULL) {
            fillin_pkg_details(pkg);
            build_package_action_menu(pkg);
        } else {
            pkg = slapt_get_pkg_by_details(installed, p_name, p_version, p_location);
            if (pkg != NULL) {
                fillin_pkg_details(pkg);
                build_package_action_menu(pkg);
            }
        }

        g_free(p_name);
        g_free(p_version);
        g_free(p_location);
    }
}

static void fillin_pkg_details(slapt_pkg_t *pkg)
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
    GtkWidget *treeview = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "dep_conf_sug_treeview"));
    slapt_pkg_t *latest_pkg = slapt_get_newest_pkg(all, pkg->name);
    slapt_pkg_t *installed_pkg = slapt_get_newest_pkg(installed, pkg->name);
    slapt_pkg_upgrade_t *pkg_upgrade = NULL;
    char *clean_desc = NULL, *changelog = NULL, *filelist = NULL, *location = NULL;
    const char *priority_str = NULL;

    /* set package details */
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_name")), pkg->name);
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_version")), pkg->version);
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_source")), pkg->mirror);
    short_desc = slapt_gen_short_pkg_description(pkg);
    if (short_desc != NULL) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_description")), short_desc);
        free(short_desc);
    } else {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_description")), "");
    }
    location = gslapt_series_map_lookup(gslapt_series_map, pkg->location);
    if (location != NULL) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_location")), location);
        free(location);
    } else {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_location")), pkg->location);
    }

    priority_str = slapt_priority_to_str(pkg->priority);
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_priority")), priority_str);

    /* dependency information tab */
    store = gtk_tree_store_new(1, G_TYPE_STRING);

    if (pkg->installed == false) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("<b>Required:</b>"), -1);

        if (pkg->required != NULL && strlen(pkg->required) > 2) {
            GtkTreeIter child_iter;
            slapt_vector_t *dependencies = slapt_parse_delimited_list(pkg->required, ',');

            slapt_vector_t_foreach (char *, buffer, dependencies) {
                gtk_tree_store_append(store, &child_iter, &iter);
                gtk_tree_store_set(store, &child_iter, 0, buffer, -1);
            }
            slapt_vector_t_free(dependencies);
        }

        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("<b>Conflicts:</b>"), -1);

        if (pkg->conflicts != NULL && strlen(pkg->conflicts) > 2) {
            GtkTreeIter child_iter;
            slapt_vector_t *conflicts = slapt_parse_delimited_list(pkg->conflicts, ',');

            slapt_vector_t_foreach (char *, buffer, conflicts) {
                gtk_tree_store_append(store, &child_iter, &iter);
                gtk_tree_store_set(store, &child_iter, 0, buffer, -1);
            }
            slapt_vector_t_free(conflicts);
        }

        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("<b>Suggests:</b>"), -1);

        if (pkg->suggests != NULL && strlen(pkg->suggests) > 2) {
            GtkTreeIter child_iter;
            slapt_vector_t *suggestions = slapt_parse_delimited_list(pkg->suggests, ',');

            slapt_vector_t_foreach (char *, buffer, suggestions) {
                gtk_tree_store_append(store, &child_iter, &iter);
                gtk_tree_store_set(store, &child_iter, 0, buffer, -1);
            }
            slapt_vector_t_free(suggestions);
        }

    } else { /* installed */
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("No dependency information available"), -1);
    }

    columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(treeview));
    for (i = 0; i < g_list_length(columns); i++) {
        GtkTreeViewColumn *my_column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns, i));
        if (my_column != NULL) {
            gtk_tree_view_remove_column(GTK_TREE_VIEW(treeview), my_column);
        }
    }
    g_list_free(columns);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Source"), renderer, "markup", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));

    /* description tab */
    pkg_full_desc = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_description_textview")));
    clean_desc = strdup(pkg->description);
    slapt_clean_description(clean_desc, pkg->name);
    gtk_text_buffer_set_text(pkg_full_desc, clean_desc, -1);

    if (clean_desc != NULL)
        free(clean_desc);

    /* changelog tab */
    pkg_changelog = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_changelog_textview")));
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
    pkg_filelist = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_filelist_textview")));
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
    if ((slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) || slapt_is_excluded(global_config, pkg) == 1) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("Excluded"));
    } else if (slapt_get_exact_pkg(trans->remove_pkgs, pkg->name, pkg->version) != NULL) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Removed"));
    } else if (slapt_get_exact_pkg(trans->install_pkgs, pkg->name, pkg->version) != NULL) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Installed"));
    } else if ((pkg_upgrade = lsearch_upgrade_transaction(trans, pkg)) != NULL) {
        if (slapt_cmp_pkgs(pkg, pkg_upgrade->installed) == 0 && slapt_cmp_pkg_versions(pkg_upgrade->upgrade->version, pkg_upgrade->installed->version) == 0) {
            gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Re-Installed"));
        } else if (slapt_cmp_pkgs(latest_pkg, pkg_upgrade->upgrade) > 0 && slapt_cmp_pkg_versions(pkg->version, pkg_upgrade->upgrade->version) > 0) {
            gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Downgraded"));
        } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) < 0) {
            gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Upgraded"));
        } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) == 0) {
            gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Installed"));
        } else if (slapt_cmp_pkgs(pkg, pkg_upgrade->upgrade) > 0) {
            gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("To be Downgraded"));
        }
    } else if (slapt_get_exact_pkg(installed, pkg->name, pkg->version) != NULL) {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("Installed"));
    } else {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_status")), (gchar *)_("Not Installed"));
    }

    /* set installed info */
    if (installed_pkg != NULL) {
        gchar size_u[20];
        sprintf(size_u, "%d K", installed_pkg->size_u);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_installed_installed_size")), size_u);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_installed_version")), installed_pkg->version);
    } else {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_installed_installed_size")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_installed_version")), "");
    }

    /* set latest available info */
    if (latest_pkg != NULL) {
        gchar latest_size_c[20], latest_size_u[20];
        sprintf(latest_size_c, "%d K", latest_pkg->size_c);
        sprintf(latest_size_u, "%d K", latest_pkg->size_u);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_version")), latest_pkg->version);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_source")), latest_pkg->mirror);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_size")), latest_size_c);
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_installed_size")), latest_size_u);
    } else {
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_version")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_source")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_size")), "");
        gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "pkg_info_available_installed_size")), "");
    }
}

static GtkWidget *progress_window;
GMutex progress_window_mutex;

struct _progress {
    double total_percent;
    double download_percent;
    char *desc;
    char *msg;
    char *action;
    char *speed;
    GMutex mutex;
};
static struct _progress progress = {0.0, 0.0, NULL, NULL, NULL, NULL};
void _progress_set_desc(char *desc)
{
    g_mutex_lock(&progress.mutex);
    if (progress.desc != NULL)
        free(progress.desc);
    progress.desc = desc;
    g_mutex_unlock(&progress.mutex);
}
void _progress_set_msg(char *msg)
{
    g_mutex_lock(&progress.mutex);
    if (progress.msg != NULL)
        free(progress.msg);
    progress.msg = msg;
    g_mutex_unlock(&progress.mutex);
}
void _progress_set_action(char *action)
{
    g_mutex_lock(&progress.mutex);
    if (progress.action != NULL)
        free(progress.action);
    progress.action = action;
    g_mutex_unlock(&progress.mutex);
}
void _progress_set_speed(char *speed)
{
    g_mutex_lock(&progress.mutex);
    if (progress.speed != NULL)
        free(progress.speed);
    progress.speed = speed;
    g_mutex_unlock(&progress.mutex);
}
void _progress_set_download_percent(double p)
{
    g_mutex_lock(&progress.mutex);
    progress.download_percent = p;
    g_mutex_unlock(&progress.mutex);
}
void _progress_set_total_percent(double p)
{
    g_mutex_lock(&progress.mutex);
    progress.total_percent = p;
    g_mutex_unlock(&progress.mutex);
}

static int _update_progress(gpointer data)
{
    g_mutex_lock(&progress.mutex);
    g_mutex_lock(&progress_window_mutex);
    GtkProgressBar *p_bar = GTK_PROGRESS_BAR(gtk_builder_get_object(gslapt_builder, "dl_progress_progressbar"));
    GtkProgressBar *dl_bar = GTK_PROGRESS_BAR(gtk_builder_get_object(gslapt_builder, "dl_progress"));
    if (p_bar == NULL || dl_bar == NULL) {
        g_mutex_unlock(&progress.mutex);
        return TRUE; /* not now, but don't cancel */
    }
    gtk_progress_bar_set_fraction(p_bar, progress.total_percent);
    gtk_progress_bar_set_fraction(dl_bar, progress.download_percent);

    GtkLabel *speed = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "progress_dl_speed"));
    if (speed != NULL) {
        if (progress.speed != NULL) {
            gtk_label_set_text(speed, progress.speed);
        } else {
            gtk_label_set_text(speed, "");
        }
    }

    GtkLabel *msg = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dl_progress_message"));
    if (msg != NULL) {
        if (progress.msg != NULL) {
            gtk_label_set_text(msg, progress.msg);
        } else {
            gtk_label_set_text(msg, "");
        }
    }

    GtkLabel *action = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dl_progress_action"));
    if (action != NULL) {
        if (progress.action != NULL) {
            gtk_label_set_text(action, progress.action);
        } else {
            gtk_label_set_text(action, "");
        }
    }

    GtkLabel *desc = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dl_progress_package_description"));
    if (desc != NULL) {
        if (progress.desc != NULL) {
            gtk_label_set_text(desc, progress.desc);
        } else {
            gtk_label_set_text(desc, "");
        }
    }
    g_mutex_unlock(&progress_window_mutex);
    g_mutex_unlock(&progress.mutex);
    return TRUE;
}

static int _get_package_data_init_progress(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    gslapt_load_ui(gslapt_builder, "dl_progress_window.ui");
    progress_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "dl_progress_window"));
    gtk_window_set_transient_for(GTK_WINDOW(progress_window), GTK_WINDOW(gslapt));
    gtk_window_set_title(GTK_WINDOW(progress_window), (gchar *)_("Progress"));
    if(!(gslapt_settings->progress_width == 0 && gslapt_settings->progress_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(progress_window), gslapt_settings->progress_width, gslapt_settings->progress_height);
    if (!(gslapt_settings->progress_x == 0 && gslapt_settings->progress_y == 0))
        gtk_window_move(GTK_WINDOW(progress_window), gslapt_settings->progress_x, gslapt_settings->progress_y);

    lock_toolbar_buttons();
    gslapt_set_status((gchar *)_("Checking for new package data..."));
    gtk_widget_show_all(progress_window);
    g_mutex_unlock(&progress_window_mutex);

    return FALSE;
}

static int _get_package_data_cancel(gpointer data) {
    g_mutex_lock(&progress_window_mutex);
    G_LOCK(_cancelled);
    _cancelled = 0;
    G_UNLOCK(_cancelled);
    gslapt_clear_status();
    gtk_widget_destroy(progress_window);
    unlock_toolbar_buttons();
    g_mutex_unlock(&progress_window_mutex);

    _progress_set_total_percent(0.0);
    _progress_set_download_percent(0.0);
    _progress_set_action(NULL);
    _progress_set_msg(NULL);
    _progress_set_desc(NULL);
    _progress_set_speed(NULL);
    unset_busy_cursor();
    return FALSE;
}

static int _get_package_data_finish(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    gtk_widget_destroy(progress_window);
    gslapt_clear_status();
    unlock_toolbar_buttons();
    rebuild_treeviews(progress_window, TRUE);
    g_mutex_unlock(&progress_window_mutex);

    _progress_set_total_percent(0.0);
    _progress_set_download_percent(0.0);
    _progress_set_action(NULL);
    _progress_set_msg(NULL);
    _progress_set_desc(NULL);
    _progress_set_speed(NULL);
    unset_busy_cursor();
    return FALSE;
}

GMutex get_package_data_source_failed_mutex;
GCond get_package_data_source_failed_cond;

static int _get_package_data_source_failed(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    g_mutex_lock(&get_package_data_source_failed_mutex);
    slapt_source_t *src = data;

    /* prompt the user to continue downloading package sources */
    gslapt_load_ui(gslapt_builder, "source_failed_dialog.ui");
    GtkWidget *q = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "source_failed_dialog"));
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "failed_source_label")), src->url);
    gtk_widget_show_all(q);

    gint result = gtk_dialog_run(GTK_DIALOG(q));
    if (result == GTK_RESPONSE_YES) {
        /* we'll disable this source and continue on */
        /* this is only disabled for the current session since slapt_config_t_write() is not called */
        src->disabled = true;
    } else {
        gtk_widget_destroy(progress_window);
        unlock_toolbar_buttons();
        gslapt_clear_status();
    }
    gtk_widget_destroy(q);

    g_cond_signal(&get_package_data_source_failed_cond);
    g_mutex_unlock(&get_package_data_source_failed_mutex);
    g_mutex_unlock(&progress_window_mutex);
    return FALSE;
}

static int _get_package_data_source_verification_failed(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    char *src_url = data;
    gslapt_clear_status();
    gtk_widget_destroy(progress_window);
    unlock_toolbar_buttons();
    notify((gchar *)_("GPG Key verification failed"), src_url);
    free(src_url);
    g_mutex_unlock(&progress_window_mutex);
    return FALSE;
}

static void get_package_data(void)
{
    gfloat dl_files = 0.0, dl_count = 0.0;
    FILE *pkg_list_fh;
    slapt_vector_t *new_pkgs = slapt_vector_t_init((slapt_vector_t_free_function)slapt_pkg_t_free);

    _progress_set_total_percent(0.0);
    _progress_set_download_percent(0.0);
    _progress_set_action(NULL);
    _progress_set_msg(NULL);
    _progress_set_desc(NULL);
    _progress_set_speed(NULL);

    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_init_progress, NULL, NULL);
    guint updater = gdk_threads_add_timeout_full(G_PRIORITY_HIGH, 20, _update_progress, NULL, NULL);
#ifdef SLAPT_HAS_GPGME
    dl_files = (global_config->sources->size * 5.0);
#else
    dl_files = (global_config->sources->size * 4.0);
#endif

    if (_cancelled == 1) {
        g_source_remove(updater);
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
        slapt_vector_t_free(new_pkgs);
        return;
    }

    /* go through each package source and download the meta data */
    slapt_vector_t_foreach (slapt_source_t *, src, global_config->sources) {
        slapt_vector_t *available_pkgs = NULL;
        slapt_vector_t *patch_pkgs = NULL;
        FILE *tmp_checksum_f = NULL;
#ifdef SLAPT_HAS_GPGME
        FILE *tmp_signature_f = NULL;
#endif
        bool compressed = 0;
        SLAPT_PRIORITY_T source_priority = src->priority;

        if (src->disabled == true)
            continue;

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
            slapt_vector_t_free(new_pkgs);
            return;
        }

        _progress_set_download_percent(0.0);
        _progress_set_msg(strdup(src->url));
        _progress_set_action(strdup((char *)_("Retrieving package data...")));

        /* download our SLAPT_PKG_LIST */
        available_pkgs = slapt_get_pkg_source_packages(global_config, src->url, &compressed);

        /* make sure we found a package listing */
        if (available_pkgs == NULL) {
            if (_cancelled == 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
                slapt_vector_t_free(new_pkgs);
                return;
            } else {
                g_mutex_lock(&get_package_data_source_failed_mutex);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_source_failed, src, NULL);
                g_cond_wait(&get_package_data_source_failed_cond, &get_package_data_source_failed_mutex);
                g_mutex_unlock(&get_package_data_source_failed_mutex);
            }
            if (src->disabled) {
                continue;
            } else {
                g_source_remove(updater);
                slapt_vector_t_free(new_pkgs);
                return;
            }
        }

        ++dl_count;

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
            slapt_vector_t_free(available_pkgs);
            slapt_vector_t_free(new_pkgs);
            return;
        }

        _progress_set_total_percent(((dl_count * 100) / dl_files) / 100);
        _progress_set_download_percent(0.0);
        _progress_set_action(strdup((char *)_("Retrieving patch list...")));

        /* download SLAPT_PATCHES_LIST */
        patch_pkgs = slapt_get_pkg_source_patches(global_config, src->url, &compressed);

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
            slapt_vector_t_free(available_pkgs);
            if (patch_pkgs)
                slapt_vector_t_free(patch_pkgs);
            slapt_vector_t_free(new_pkgs);
            return;
        }

        ++dl_count;

        _progress_set_total_percent(((dl_count * 100) / dl_files) / 100);
        _progress_set_download_percent(0.0);
        _progress_set_action(strdup((char *)_("Retrieving checksum list...")));

        /* download checksum file */
        tmp_checksum_f = slapt_get_pkg_source_checksums(global_config, src->url, &compressed);

        if (tmp_checksum_f == NULL) {
            if (_cancelled == 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
                slapt_vector_t_free(available_pkgs);
                if (patch_pkgs)
                    slapt_vector_t_free(patch_pkgs);
                slapt_vector_t_free(new_pkgs);
                return;
            } else {
                g_mutex_lock(&get_package_data_source_failed_mutex);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_source_failed, src, NULL);
                g_cond_wait(&get_package_data_source_failed_cond, &get_package_data_source_failed_mutex);
                g_mutex_unlock(&get_package_data_source_failed_mutex);
            }
            if (src->disabled) {
                continue;
            } else {
                g_source_remove(updater);
                slapt_vector_t_free(available_pkgs);
                if (patch_pkgs)
                    slapt_vector_t_free(patch_pkgs);
                slapt_vector_t_free(new_pkgs);
                return;
            }
        }

        ++dl_count;

#ifdef SLAPT_HAS_GPGME
        _progress_set_total_percent(((dl_count * 100) / dl_files) / 100);
        _progress_set_download_percent(0.0);
        _progress_set_action(strdup((char *)_("Retrieving checksum signature...")));

        tmp_signature_f = slapt_get_pkg_source_checksums_signature(global_config, src->url, &compressed);

        if (tmp_signature_f == NULL) {
            if (_cancelled == 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_cancel, NULL, NULL);
                slapt_vector_t_free(available_pkgs);
                if (patch_pkgs)
                    slapt_vector_t_free(patch_pkgs);
                slapt_vector_t_free(new_pkgs);
                return;
            }
        } else {
            FILE *tmp_checksum_to_verify_f = NULL;

            /* if we downloaded the compressed checksums, open it raw (w/o gunzippign) */
            if (compressed) {
                char *filename = slapt_gen_filename_from_url(src->url, SLAPT_CHECKSUM_FILE_GZ);
                tmp_checksum_to_verify_f = slapt_open_file(filename, "r");
                free(filename);
            } else {
                tmp_checksum_to_verify_f = tmp_checksum_f;
            }

            if (tmp_checksum_to_verify_f != NULL) {
                slapt_code_t verified = SLAPT_CHECKSUMS_NOT_VERIFIED;
                _progress_set_action(strdup((char *)_("Verifying checksum signature...")));
                verified = slapt_gpg_verify_checksums(tmp_checksum_to_verify_f, tmp_signature_f);
                if (verified == SLAPT_CHECKSUMS_NOT_VERIFIED) {
                    fclose(tmp_checksum_f);
                    fclose(tmp_signature_f);
                    if (compressed)
                        fclose(tmp_checksum_to_verify_f);
                    g_source_remove(updater);
                    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_source_verification_failed, strdup(src->url), NULL);
                    slapt_vector_t_free(available_pkgs);
                    if (patch_pkgs)
                        slapt_vector_t_free(patch_pkgs);
                    slapt_vector_t_free(new_pkgs);
                    return;
                }
            }

            fclose(tmp_signature_f);

            /* if we opened the raw gzipped checksums, close it here */
            if (compressed)
                fclose(tmp_checksum_to_verify_f);
            else
                rewind(tmp_checksum_f);
        }

        ++dl_count;
#endif

        /* download changelog file */
        _progress_set_total_percent(((dl_count * 100) / dl_files) / 100);
        _progress_set_download_percent(0.0);
        _progress_set_action(strdup((char *)_("Retrieving ChangeLog.txt...")));

        slapt_get_pkg_source_changelog(global_config, src->url, &compressed);

        _progress_set_total_percent(((dl_count * 100) / dl_files) / 100);
        _progress_set_download_percent(0.0);
        _progress_set_action(strdup((char *)_("Reading Package Lists...")));

        if (available_pkgs) {
            /* map packages to md5sums */
            slapt_get_md5sums(available_pkgs, tmp_checksum_f);

            /* put these into our new package list */
            slapt_vector_t_foreach (slapt_pkg_t *, apkg, available_pkgs) {
                /* honor the mirror if it was set in the PACKAGES.TXT */
                if (apkg->mirror == NULL || strlen(apkg->mirror) == 0) {
                    if (apkg->mirror != NULL) {
                        free(apkg->mirror);
                    }
                    apkg->mirror = strdup(src->url);
                }
                /* set the priority of the package based on the source */
                apkg->priority = source_priority;
                slapt_vector_t_add(new_pkgs, apkg);
            }

            /* don't free the slapt_pkg_t objects as they are now part of new_pkgs */
            available_pkgs->free_function = NULL;
            slapt_vector_t_free(available_pkgs);
        }

        if (patch_pkgs) {
            /* map packages to md5sums */
            slapt_get_md5sums(patch_pkgs, tmp_checksum_f);

            /* put these into our new package list */
            slapt_vector_t_foreach (slapt_pkg_t *, ppkg, patch_pkgs) {
                /* honor the mirror if it was set in the PACKAGES.TXT */
                if (ppkg->mirror == NULL || strlen(ppkg->mirror) == 0) {
                    if (ppkg->mirror != NULL) {
                        free(ppkg->mirror);
                    }
                    ppkg->mirror = strdup(src->url);
                }
                /* set the priority of the package based on the source, plus 1 for the patch priority */
                if (global_config->use_priority == true)
                    ppkg->priority = source_priority + 1;
                else
                    ppkg->priority = source_priority;
                slapt_vector_t_add(new_pkgs, ppkg);
            }

            /* don't free the slapt_pkg_t objects as they are now part of new_pkgs */
            patch_pkgs->free_function = NULL;
            slapt_vector_t_free(patch_pkgs);
        }

        fclose(tmp_checksum_f);

    } /* end for loop */

    /* if all our downloads where a success, write to SLAPT_PKG_LIST_L */
    if ((pkg_list_fh = slapt_open_file(SLAPT_PKG_LIST_L, "w+")) == NULL)
        exit(1);
    slapt_write_pkg_data(NULL, pkg_list_fh, new_pkgs);
    fclose(pkg_list_fh);

    slapt_vector_t_free(new_pkgs);

    /* reset our currently selected packages */
    slapt_free_transaction(trans);
    trans = slapt_init_transaction();

    g_source_remove(updater);
    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_package_data_finish, NULL, NULL);
}

int gtk_progress_callback(void *data, double dltotal, double dlnow, double ultotal, double ulnow)
{
    double percent = 1.0;
    struct slapt_progress_data *cb_data = (struct slapt_progress_data *)data;
    time_t now = time(NULL);
    double elapsed = now - cb_data->start;
    double speed = dlnow / (elapsed > 0 ? elapsed : 1);
    gchar *status = NULL;

    if (_cancelled == 1) {
        return -1;
    }

    if (dltotal != 0.0)
        percent = ((dlnow * 100) / dltotal) / 100;

    status = g_strdup_printf((gchar *)_("Download rate: %.0f%s/s"),
                             (speed > 1000) ? (speed > 1000000) ? speed / 1000000 : speed / 1000 : speed,
                             (speed > 1000) ? (speed > 1000000) ? "M" : "k" : "b");

    _progress_set_speed(status);
    _progress_set_download_percent(percent);

    return 0;
}

static void rebuild_treeviews(GtkWidget *current_window, gboolean reload)
{
    GtkWidget *treeview;
    GtkListStore *store;
    GtkTreeModelFilter *filter_model;
    GtkTreeModelSort *package_model;
    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));

    set_busy_cursor();

    if (reload == TRUE) {
        slapt_vector_t *all_ptr, *installed_ptr;

        installed_ptr = installed;
        all_ptr = all;

        installed = slapt_get_installed_pkgs();
        all = slapt_get_available_pkgs();

        slapt_vector_t_free(installed_ptr);
        slapt_vector_t_free(all_ptr);
    }

    treeview = (GtkWidget *)gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview");
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    store = GTK_LIST_STORE(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
    gtk_list_store_clear(store);

    if (reload == TRUE) {
        gtk_entry_set_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")), "");
    }

    rebuild_package_action_menu();
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_all_packages_menu")), TRUE);
    build_package_treeviewlist(treeview);

    if ((reload == FALSE) && (strcmp(search_text, "") != 0)) {
        gchar *search = g_strdup(search_text);
        build_searched_treeviewlist(GTK_WIDGET(treeview), search);
        g_free(search);
    }
}

static void gslapt_set_status(const gchar *msg)
{
    gslapt_clear_status();
    GtkStatusbar *bar = GTK_STATUSBAR(gtk_builder_get_object(gslapt_builder, "bottom_statusbar"));
    gtk_statusbar_push(bar, 2, msg);
}

static void gslapt_clear_status(void)
{
    GtkStatusbar *bar = GTK_STATUSBAR(gtk_builder_get_object(gslapt_builder, "bottom_statusbar"));
    gtk_statusbar_pop(bar, 2);
}

static void lock_toolbar_buttons(void)
{
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "top_menubar")), FALSE);

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_update_button")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_upgrade_button")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_execute_button")), FALSE);
}

static void unlock_toolbar_buttons(void)
{
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "top_menubar")), TRUE);

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_update_button")), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_upgrade_button")), TRUE);

    if (trans->upgrade_pkgs->size != 0 || trans->remove_pkgs->size != 0 || trans->install_pkgs->size != 0 || trans->reinstall_pkgs->size != 0) {
        set_execute_active();
    }
}

static int _lhandle_transaction_dl_failed(gpointer data)
{
    if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
    }

    char *err = data;
    if (err != NULL) {
        notify((gchar *)_("Error"), (gchar *)err);
        free(err);
    }
    unlock_toolbar_buttons();
    unset_busy_cursor();
    return FALSE;
}

static int _lhandle_transaction_dl_finish(gpointer data)
{
    if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
    }
    clear_execute_active();
    unlock_toolbar_buttons();
    unset_busy_cursor();

    return FALSE;
}

static int _lhandle_transaction_dl_complete(gpointer data)
{
    /* set busy cursor */
    set_busy_cursor();
    clear_execute_active();

    slapt_free_transaction(trans);
    trans = slapt_init_transaction();

    /* rebuild the installed list */
    slapt_vector_t *installed_ptr = installed;
    installed = slapt_get_installed_pkgs();
    slapt_vector_t_free(installed_ptr);

    rebuild_treeviews(NULL, FALSE);
    rebuild_package_action_menu();
    unlock_toolbar_buttons();
    /* reset cursor */
    unset_busy_cursor();

    notify((gchar *)_("Completed actions"), (gchar *)_("Successfully executed all actions."));

    return FALSE;
}

static void lhandle_transaction(gpointer data)
{
    gboolean dl_only = (intptr_t)data;

    /* download the pkgs */
    if (trans->install_pkgs->size > 0 || trans->upgrade_pkgs->size > 0 || trans->reinstall_pkgs->size > 0) {
        char *err = download_packages();
        if (err != NULL || _cancelled == 1) {
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _lhandle_transaction_dl_failed, err, NULL);
            return;
        }
    }

    /* return early if download_only is set */
    if (dl_only == TRUE || _cancelled == 1) {
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _lhandle_transaction_dl_finish, NULL, NULL);
        return;
    }

    /* begin removing, installing, and upgrading */
    if (install_packages() == FALSE) {
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _lhandle_transaction_dl_failed, _("pkgtools returned an error"), NULL);
        return;
    }

    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _lhandle_transaction_dl_complete, NULL, NULL);
}

void transaction_okbutton_clicked(GtkWidget *w, gpointer user_data)
{
    GThread *gdp;
    lock_toolbar_buttons();
    GtkCheckButton *dl_only_checkbutton = GTK_CHECK_BUTTON(gtk_builder_get_object(gslapt_builder, "download_only_checkbutton"));
    gboolean dl_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dl_only_checkbutton));
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "transaction_window")));

#if !GLIB_CHECK_VERSION(2, 31, 0)
    gdp = g_thread_create((GThreadFunc)lhandle_transaction, (void *)(intptr_t)dl_only, FALSE, NULL);
#else
    gdp = g_thread_new("GslaptTransactionStart", (GThreadFunc)lhandle_transaction, (void *)(intptr_t)dl_only);
    g_thread_unref(gdp);
#endif

    return;
}

static void build_sources_treeviewlist(GtkWidget *treeview)
{
    GtkListStore *store;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;
    GdkPixbuf *enabled_status_icon = gslapt_img("pkg_action_installed.png");
    GdkPixbuf *disabled_status_icon = gslapt_img("pkg_action_available.png");
    gboolean enabled = TRUE;

    store = gtk_list_store_new(5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT);

    slapt_vector_t_foreach (slapt_source_t *, src, global_config->sources) {
        GdkPixbuf *status_icon;
        const char *priority_str;

        if (src->url == NULL)
            continue;

        if (src->disabled == true) {
            enabled = FALSE;
            status_icon = disabled_status_icon;
        } else {
            enabled = TRUE;
            status_icon = enabled_status_icon;
        }

        priority_str = slapt_priority_to_str(src->priority);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, status_icon, 1, src->url, 2, enabled, 3, priority_str, 4, src->priority, -1);
        // g_object_unref(status_icon);
    }

    /* column for enabled status */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Enabled"), renderer, "pixbuf", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    /* column for url */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Source"), renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    /* enabled/disabled bool column */
    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Visible"), renderer, "radio", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_visible(column, FALSE);

    /* priority column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Priority"), renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
    g_object_unref(store);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
    g_signal_handlers_disconnect_by_func(G_OBJECT(treeview), toggle_source_status, NULL);
    g_signal_connect(G_OBJECT(treeview), "cursor-changed", G_CALLBACK(toggle_source_status), NULL);
}

static void build_exclude_treeviewlist(GtkWidget *treeview)
{
    GtkListStore *store;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;

    store = gtk_list_store_new(
        1, /* exclude expression */
        G_TYPE_STRING);

    slapt_vector_t_foreach (char *, exclude, global_config->exclude_list) {
        if (exclude == NULL) {
            continue;
        }
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, exclude, -1);
    }

    /* column for url */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Expression"), renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
    g_object_unref(store);
    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
}

static int populate_transaction_window(void)
{
    GtkTreeView *summary_treeview;
    GtkTreeStore *store;
    GtkTreeIter iter, child_iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkLabel *sum_pkg_num, *sum_dl_size, *sum_free_space;
    double dl_size = 0, free_space = 0, already_dl_size = 0;
    gchar buf[512];

    summary_treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "transaction_summary_treeview"));
    store = gtk_tree_store_new(1, G_TYPE_STRING);
    sum_pkg_num = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "summary_pkg_numbers"));
    sum_dl_size = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "summary_dl_size"));
    sum_free_space = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "summary_free_space"));

    /* setup the store */
    if (trans->missing_err->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages with unmet dependencies"), -1);

        slapt_vector_t_foreach (slapt_pkg_err_t *, missing_err, trans->missing_err) {
            gchar *err = g_strdup_printf("%s: Depends: %s", missing_err->pkg, missing_err->error);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, err, -1);
            g_free(err);
        }
    }

    if (trans->conflict_err->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Package conflicts"), -1);

        slapt_vector_t_foreach (slapt_pkg_err_t *, conflict_err, trans->conflict_err) {
            gchar *err = g_strdup_printf("%s%s%s%s",
                                         conflict_err->error,
                                         (gchar *)_(", which is required by "),
                                         conflict_err->pkg,
                                         (gchar *)_(", is excluded"));

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, err, -1);
            g_free(err);
        }
    }

    if (trans->exclude_pkgs->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages excluded"), -1);

        slapt_vector_t_foreach (slapt_pkg_t *, exclude_pkg, trans->exclude_pkgs) {
            gchar *detail = g_strdup_printf("%s %s", exclude_pkg->name, exclude_pkg->version);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, detail, -1);

            g_free(detail);
        }
    }

    if (trans->install_pkgs->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages to be installed"), -1);

        slapt_vector_t_foreach (slapt_pkg_t *, install_pkg, trans->install_pkgs) {
            gchar *detail = g_strdup_printf("%s %s", install_pkg->name, install_pkg->version);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, detail, -1);
            dl_size += install_pkg->size_c;
            already_dl_size += slapt_get_pkg_file_size(global_config, install_pkg) / 1024;
            free_space += install_pkg->size_u;

            g_free(detail);
        }
    }

    if (trans->upgrade_pkgs->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages to be upgraded"), -1);

        slapt_vector_t_foreach (slapt_pkg_upgrade_t *, upgrade_pkg, trans->upgrade_pkgs) {
            gchar *detail = g_strdup_printf("%s (%s) -> %s",
                                            upgrade_pkg->installed->name,
                                            upgrade_pkg->installed->version,
                                            upgrade_pkg->upgrade->version);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, detail, -1);

            dl_size += upgrade_pkg->upgrade->size_c;
            already_dl_size += slapt_get_pkg_file_size(global_config, upgrade_pkg->upgrade) / 1024;
            free_space += upgrade_pkg->upgrade->size_u;
            free_space -= upgrade_pkg->installed->size_u;

            g_free(detail);
        }
    }

    if (trans->reinstall_pkgs->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages to be reinstalled"), -1);
        slapt_vector_t_foreach (slapt_pkg_upgrade_t *, reinstall_pkg, trans->reinstall_pkgs) {
            gchar *detail = g_strdup_printf("%s %s", reinstall_pkg->upgrade->name, reinstall_pkg->upgrade->version);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, detail, -1);

            dl_size += reinstall_pkg->upgrade->size_c;
            already_dl_size += slapt_get_pkg_file_size(global_config, reinstall_pkg->upgrade) / 1024;
            free_space += reinstall_pkg->upgrade->size_u;
            free_space -= reinstall_pkg->installed->size_u;

            g_free(detail);
        }
    }

    if (trans->remove_pkgs->size > 0) {
        gtk_tree_store_append(store, &iter, NULL);
        gtk_tree_store_set(store, &iter, 0, _("Packages to be removed"), -1);

        slapt_vector_t_foreach (slapt_pkg_t *, remove_pkg, trans->remove_pkgs) {
            gchar *detail = g_strdup_printf("%s %s", remove_pkg->name, remove_pkg->version);

            gtk_tree_store_append(store, &child_iter, &iter);
            gtk_tree_store_set(store, &child_iter, 0, detail, -1);
            free_space -= remove_pkg->size_u;

            g_free(detail);
        }
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(summary_treeview), GTK_TREE_MODEL(store));
    g_object_unref(store);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Package"), renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(summary_treeview), column);

    char fmt[512];
    snprintf(fmt, 512, "%s%s%s%s%s",
             P_("%d upgraded, ", "%d upgraded, ", trans->upgrade_pkgs->size),
             P_("%d reinstalled, ", "%d reinstalled, ", trans->reinstall_pkgs->size),
             P_("%d newly installed, ", "%d newly installed, ", trans->install_pkgs->size),
             P_("%d to remove ", "%d to remove ", trans->remove_pkgs->size),
             P_("and %d not upgraded.", "and %d not upgraded.", trans->exclude_pkgs->size));
    snprintf(buf, 512, fmt,
             trans->upgrade_pkgs->size,
             trans->reinstall_pkgs->size,
             trans->install_pkgs->size,
             trans->remove_pkgs->size,
             trans->exclude_pkgs->size);
    gtk_label_set_text(GTK_LABEL(sum_pkg_num), buf);

    /* if we don't have enough free space to download */
    if (slapt_disk_space_check(global_config->working_dir, dl_size - already_dl_size) == false) {
        notify((gchar *)_("Error"), (gchar *)_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
        return -1;
    }
    /* if we don't have enough free space to install on / */
    if (slapt_disk_space_check("/", free_space) == false) {
        notify((gchar *)_("Error"), (gchar *)_("<span weight=\"bold\" size=\"large\">You don't have enough free space</span>"));
        return -1;
    }

    if (already_dl_size > 0) {
        double need_to_dl = dl_size - already_dl_size;
        snprintf(buf, 512, (gchar *)_("Need to get %.0f%s/%.0f%s of archives.\n"),
                 (need_to_dl > 1024) ? need_to_dl / 1024
                                     : need_to_dl,
                 (need_to_dl > 1024) ? "MB" : "kB",
                 (dl_size > 1024) ? dl_size / 1024 : dl_size,
                 (dl_size > 1024) ? "MB" : "kB");
    } else {
        snprintf(buf, 512, (gchar *)_("Need to get %.0f%s of archives."),
                 (dl_size > 1024) ? dl_size / 1024 : dl_size,
                 (dl_size > 1024) ? "MB" : "kB");
    }
    gtk_label_set_text(GTK_LABEL(sum_dl_size), buf);

    if (free_space < 0) {
        free_space *= -1;
        snprintf(buf, 512, (gchar *)_("After unpacking %.0f%s disk space will be freed."),
                 (free_space > 1024) ? free_space / 1024
                                     : free_space,
                 (free_space > 1024) ? "MB" : "kB");
    } else {
        snprintf(buf, 512, (gchar *)_("After unpacking %.0f%s of additional disk space will be used."),
                 (free_space > 1024) ? free_space / 1024 : free_space,
                 (free_space > 1024) ? "MB" : "kB");
    }
    gtk_label_set_text(GTK_LABEL(sum_free_space), buf);

    return 0;
}

static void mark_upgrade_packages(void)
{
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    guint mark_count = 0;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    slapt_vector_t_foreach (slapt_pkg_t *, installed_pkg, installed) {
        slapt_pkg_t *update_pkg = NULL;
        slapt_pkg_t *newer_installed_pkg = NULL;

        /* we need to see if there is another installed package that is newer than this one */
        if ((newer_installed_pkg = slapt_get_newest_pkg(installed, installed_pkg->name)) != NULL) {
            if (slapt_cmp_pkgs(installed_pkg, newer_installed_pkg) < 0)
                continue;
        }

        /* see if we have an available update for the pkg */
        update_pkg = slapt_get_newest_pkg(all, installed_pkg->name);
        if (update_pkg != NULL) {
            int cmp_r = 0;

            /* if the update has a newer version, attempt to upgrade */
            cmp_r = slapt_cmp_pkgs(installed_pkg, update_pkg);
            /* either it's greater, or we want to reinstall */
            if (cmp_r < 0 || (global_config->re_install == TRUE)) {
                if ((slapt_is_excluded(global_config, update_pkg) == 1) || (slapt_is_excluded(global_config, installed_pkg) == 1)) {
                    slapt_add_exclude_to_transaction(trans, update_pkg);
                } else {
                    slapt_vector_t *conflicts = slapt_is_conflicted(trans, all, installed, update_pkg);

                    /* if all deps are added and there is no conflicts, add on */
                    if ((ladd_deps_to_trans(trans, all, installed, update_pkg) == 0) && (conflicts->size == 0)) {
                        if (global_config->re_install == TRUE)
                            slapt_add_reinstall_to_transaction(trans, installed_pkg, update_pkg);
                        else
                            slapt_add_upgrade_to_transaction(trans, installed_pkg, update_pkg);

                        if (set_iter_to_pkg(base_model, &iter, update_pkg)) {
                            set_iter_for_upgrade(base_model, &iter, update_pkg);
                        }
                        ++mark_count;
                    } else {
                        /* otherwise exclude */
                        slapt_add_exclude_to_transaction(trans, update_pkg);
                    }
                    slapt_vector_t_free(conflicts);
                }
            }

        } /* end upgrade pkg found */

    } /* end for */

    if (mark_count == 0) {
        notify((gchar *)_("Up to Date"), (gchar *)_("<span weight=\"bold\" size=\"large\">No updates available</span>"));
    }
}


static int _download_packages_init_progress(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    gslapt_load_ui(gslapt_builder, "dl_progress_window.ui");
    progress_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "dl_progress_window"));
    gtk_window_set_transient_for(GTK_WINDOW(progress_window), GTK_WINDOW(gslapt));
    gtk_window_set_title(GTK_WINDOW(progress_window), (gchar *)_("Progress"));
    if(!(gslapt_settings->progress_width == 0 && gslapt_settings->progress_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(progress_window), gslapt_settings->progress_width, gslapt_settings->progress_height);
    if (!(gslapt_settings->progress_x == 0 && gslapt_settings->progress_y == 0))
        gtk_window_move(GTK_WINDOW(progress_window), gslapt_settings->progress_x, gslapt_settings->progress_y);
    gtk_widget_show_all(progress_window);
    gslapt_set_status((gchar *)_("Downloading packages..."));
    g_mutex_unlock(&progress_window_mutex);
    return FALSE;
}
static int _download_packages_cancel(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    G_LOCK(_cancelled);
    _cancelled = 0;
    G_UNLOCK(_cancelled);

    gtk_widget_destroy(progress_window);
    progress_window = NULL;
    gslapt_clear_status();

    _progress_set_total_percent(0.0);
    _progress_set_action(NULL);
    _progress_set_msg(NULL);
    _progress_set_desc(NULL);
    _progress_set_download_percent(0.0);
    g_mutex_unlock(&progress_window_mutex);
    return FALSE;
}

char *download_packages(void)
{
    gfloat pkgs_to_dl = 0.0, count = 0.0;
    const char *err = NULL;

    pkgs_to_dl += trans->install_pkgs->size;
    pkgs_to_dl += trans->upgrade_pkgs->size;
    pkgs_to_dl += trans->reinstall_pkgs->size;

    _progress_set_total_percent(((count * 100) / pkgs_to_dl) / 100);
    _progress_set_download_percent(0.0);
    _progress_set_action(NULL);
    _progress_set_msg(NULL);
    _progress_set_desc(NULL);
    _progress_set_speed(NULL);

    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_init_progress, NULL, NULL);
    guint updater = gdk_threads_add_timeout_full(G_PRIORITY_HIGH, 20, _update_progress, NULL, NULL);

    if (_cancelled == 1) {
        g_source_remove(updater);
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
        return NULL;
    }

    slapt_vector_t_foreach (slapt_pkg_t *, install_pkg, trans->install_pkgs) {
        guint msg_len = strlen(install_pkg->name) + strlen("-") + strlen(install_pkg->version) + strlen(".") + strlen(install_pkg->file_ext);
        gchar *msg = slapt_malloc(msg_len * sizeof *msg);

        snprintf(msg,
                 strlen(install_pkg->name) + strlen("-") + strlen(install_pkg->version) + strlen(".") + strlen(install_pkg->file_ext),
                 "%s-%s%s",
                 install_pkg->name,
                 install_pkg->version,
                 install_pkg->file_ext);

        _progress_set_action(strdup((char *)_("Downloading...")));
        _progress_set_msg(strdup(msg));
        _progress_set_desc(strdup(install_pkg->mirror));
        _progress_set_download_percent(0.0);
        _progress_set_total_percent(((count * 100) / pkgs_to_dl) / 100);
        free(msg);

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
            return NULL;
        }

        err = slapt_download_pkg(global_config, install_pkg, NULL);
        if (err) {
            if (_cancelled != 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
                return g_strdup_printf(_("Failed to download %s: %s"), install_pkg->name, err);
            }
        }
        ++count;
    }

    slapt_vector_t_foreach (slapt_pkg_upgrade_t *, upgrade_pkg, trans->upgrade_pkgs) {
        guint msg_len = strlen(upgrade_pkg->upgrade->name) + strlen("-") + strlen(upgrade_pkg->upgrade->version) + strlen(".") + strlen(upgrade_pkg->upgrade->file_ext);
        gchar *msg = slapt_malloc(sizeof *msg * msg_len);
        gchar dl_size[20];

        snprintf(msg,
                 strlen(upgrade_pkg->upgrade->name) + strlen("-") + strlen(upgrade_pkg->upgrade->version) + strlen(".") + strlen(upgrade_pkg->upgrade->file_ext),
                 "%s-%s%s",
                 upgrade_pkg->upgrade->name,
                 upgrade_pkg->upgrade->version,
                 upgrade_pkg->upgrade->file_ext);
        sprintf(dl_size, "%d K", upgrade_pkg->upgrade->size_c);

        _progress_set_action(strdup((char *)_("Downloading...")));
        _progress_set_msg(strdup(msg));
        _progress_set_desc(strdup(upgrade_pkg->upgrade->mirror));
        _progress_set_download_percent(0.0);
        _progress_set_total_percent(((count * 100) / pkgs_to_dl) / 100);
        free(msg);

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
            return NULL;
        }

        err = slapt_download_pkg(global_config, upgrade_pkg->upgrade, NULL);
        if (err) {
            if (_cancelled != 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
                return g_strdup_printf(_("Failed to download %s: %s"), upgrade_pkg->upgrade->name, err);
            }
        }
        ++count;
    }

    slapt_vector_t_foreach (slapt_pkg_upgrade_t *, reinstall_pkg, trans->reinstall_pkgs) {
        guint msg_len = strlen(reinstall_pkg->upgrade->name) + strlen("-") + strlen(reinstall_pkg->upgrade->version) + strlen(".") + strlen(reinstall_pkg->upgrade->file_ext);
        gchar *msg = slapt_malloc(sizeof *msg * msg_len);
        gchar dl_size[20];

        snprintf(msg,
                 strlen(reinstall_pkg->upgrade->name) + strlen("-") + strlen(reinstall_pkg->upgrade->version) + strlen(".") + strlen(reinstall_pkg->upgrade->file_ext),
                 "%s-%s%s",
                 reinstall_pkg->upgrade->name,
                 reinstall_pkg->upgrade->version,
                 reinstall_pkg->upgrade->file_ext);
        sprintf(dl_size, "%d K", reinstall_pkg->upgrade->size_c);

        _progress_set_action(strdup((char *)_("Downloading...")));
        _progress_set_msg(strdup(msg));
        _progress_set_desc(strdup(reinstall_pkg->upgrade->mirror));
        _progress_set_download_percent(0.0);
        _progress_set_total_percent(((count * 100) / pkgs_to_dl) / 100);
        free(msg);

        if (_cancelled == 1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
            return NULL;
        }

        err = slapt_download_pkg(global_config, reinstall_pkg->upgrade, NULL);
        if (err) {
            if (_cancelled != 1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
                return g_strdup_printf(_("Failed to download %s: %s"), reinstall_pkg->upgrade->name, err);
            }
        }
        ++count;
    }
    g_source_remove(updater);
    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _download_packages_cancel, NULL, NULL);
    return NULL;
}

static GtkWidget *pkgtools_progress_window;
GMutex pkgtools_progress_window_mutex;

struct _pkgtools_status {
    double total_percent;
    char *desc;
    char *msg;
    char *action;
    GMutex mutex;
};
static struct _pkgtools_status pkgtools_status = {0.0, NULL, NULL, NULL};
void _pkgtools_status_set_total_percent(double percent)
{
    g_mutex_lock(&pkgtools_status.mutex);
    pkgtools_status.total_percent = percent;
    g_mutex_unlock(&pkgtools_status.mutex);
}
void _pkgtools_status_set_desc(char *desc)
{
    g_mutex_lock(&pkgtools_status.mutex);
    if (pkgtools_status.desc != NULL)
        free(pkgtools_status.desc);
    pkgtools_status.desc = desc;
    g_mutex_unlock(&pkgtools_status.mutex);
}

void _pkgtools_status_set_msg(char *msg)
{
    g_mutex_lock(&pkgtools_status.mutex);
    if (pkgtools_status.msg != NULL)
        free(pkgtools_status.msg);
    pkgtools_status.msg = msg;
    g_mutex_unlock(&pkgtools_status.mutex);
}

void _pkgtools_status_set_action(char *action)
{
    g_mutex_lock(&pkgtools_status.mutex);
    if (pkgtools_status.action != NULL)
        free(pkgtools_status.action);
    pkgtools_status.action = action;
    g_mutex_unlock(&pkgtools_status.mutex);
}

static int _update_pkgtools_progress(gpointer data)
{
    g_mutex_lock(&pkgtools_status.mutex);
    g_mutex_lock(&pkgtools_progress_window_mutex);
    if (pkgtools_progress_window == NULL) {
        g_mutex_unlock(&pkgtools_progress_window_mutex);
        g_mutex_unlock(&pkgtools_status.mutex);
        return TRUE; /* not now, but don't cancel */
    }

    GtkProgressBar *p_bar = GTK_PROGRESS_BAR(gtk_builder_get_object(gslapt_builder, "progress_progressbar"));
    if (p_bar == NULL) {
        g_mutex_unlock(&pkgtools_progress_window_mutex);
        g_mutex_unlock(&pkgtools_status.mutex);
        return TRUE; /* not now, but don't cancel */
    }
    gtk_progress_bar_set_fraction(p_bar, pkgtools_status.total_percent);

    GtkLabel *action = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "progress_action"));
    if (action != NULL) {
        if (pkgtools_status.action != NULL) {
            gtk_label_set_text(action, pkgtools_status.action);
        } else{
            gtk_label_set_text(action, "");
        }
    }
    GtkLabel *msg = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "progress_message"));
    if (msg != NULL) {
        if (pkgtools_status.msg != NULL) {
            gtk_label_set_text(msg, pkgtools_status.msg);
        } else{
            gtk_label_set_text(msg, "");
        }
    }
    GtkLabel *desc = GTK_LABEL(gtk_builder_get_object(gslapt_builder, "progress_package_description"));
    if (desc != NULL) {
        if (pkgtools_status.desc != NULL) {
            gtk_label_set_text(desc, pkgtools_status.desc);
        } else{
            gtk_label_set_text(desc, "");
        }
    }

    g_mutex_unlock(&pkgtools_progress_window_mutex);
    g_mutex_unlock(&pkgtools_status.mutex);
    return TRUE;
}

static int _install_packages_init(gpointer data)
{
    g_mutex_lock(&pkgtools_progress_window_mutex);
    gslapt_load_ui(gslapt_builder, "pkgtools_progress_window.ui");
    pkgtools_progress_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "pkgtools_progress_window"));
    gtk_window_set_transient_for(GTK_WINDOW(pkgtools_progress_window), GTK_WINDOW(gslapt));
    if(!(gslapt_settings->progress_width == 0 && gslapt_settings->progress_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(progress_window), gslapt_settings->progress_width, gslapt_settings->progress_height);
    if (!(gslapt_settings->progress_x == 0 && gslapt_settings->progress_y == 0))
        gtk_window_move(GTK_WINDOW(progress_window), gslapt_settings->progress_x, gslapt_settings->progress_y);
    gtk_window_move(GTK_WINDOW(pkgtools_progress_window), gslapt_settings->progress_x, gslapt_settings->progress_y);
    gtk_widget_show_all(pkgtools_progress_window);
    g_mutex_unlock(&pkgtools_progress_window_mutex);

    return FALSE;
}

static int _install_packages_set_removing(gpointer data)
{
    gslapt_set_status((char *)_("Removing packages..."));
    return FALSE;
}

static int _install_packages_set_installing(gpointer data)
{
    gslapt_set_status((char *)_("Installing packages..."));
    return FALSE;
}

static int _install_packages_failed(gpointer data)
{
    g_mutex_lock(&pkgtools_progress_window_mutex);
    gslapt_clear_status();
    gtk_widget_destroy(pkgtools_progress_window);
    g_mutex_unlock(&pkgtools_progress_window_mutex);

    _pkgtools_status_set_desc(NULL);
    _pkgtools_status_set_action(NULL);
    _pkgtools_status_set_msg(NULL);
    _pkgtools_status_set_total_percent(0.0);
    return FALSE;
}

static int _install_packages_complete(gpointer data)
{
    g_mutex_lock(&pkgtools_progress_window_mutex);
    gslapt_clear_status();
    gtk_widget_destroy(pkgtools_progress_window);
    pkgtools_progress_window = NULL;
    g_mutex_unlock(&pkgtools_progress_window_mutex);

    _pkgtools_status_set_desc(NULL);
    _pkgtools_status_set_action(NULL);
    _pkgtools_status_set_msg(NULL);
    _pkgtools_status_set_total_percent(0.0);
    return FALSE;
}

static gboolean install_packages(void)
{
    gfloat count = 0.0;

    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_init, NULL, NULL);
    guint updater = gdk_threads_add_timeout_full(G_PRIORITY_HIGH, 20, _update_pkgtools_progress, NULL, NULL);

    _pkgtools_status_set_desc(NULL);
    _pkgtools_status_set_action(NULL);
    _pkgtools_status_set_msg(NULL);
    _pkgtools_status_set_total_percent(0.0);

    /* begin removing, installing, and upgrading */
    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_set_removing, NULL, NULL);
    slapt_vector_t_foreach (slapt_pkg_t *, remove_pkg, trans->remove_pkgs) {
        char *clean_desc = strdup(remove_pkg->description);
        slapt_clean_description(clean_desc, remove_pkg->name);

        if (clean_desc != NULL)
            _pkgtools_status_set_desc(clean_desc);
        _pkgtools_status_set_action(strdup((char *)_("Uninstalling...")));
        _pkgtools_status_set_msg(strdup(remove_pkg->name));
        _pkgtools_status_set_total_percent(((count * 100) / trans->remove_pkgs->size) / 100);

        if (slapt_remove_pkg(global_config, remove_pkg) == -1) {
            g_source_remove(updater);
            gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_failed, NULL, NULL);
            return FALSE;
        }
        ++count;
    }

    /* reset progress bar */
    _pkgtools_status_set_total_percent(0.0);

    if (trans->queue->size == 0) {
        g_source_remove(updater);
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_complete, NULL, NULL);
        return TRUE;
    }

    /* now for the installs and upgrades */
    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_set_installing, NULL, NULL);
    count = 0.0;
    slapt_vector_t_foreach (slapt_queue_i *, qi, trans->queue) {

        if (qi->type == SLAPT_ACTION_INSTALL) {
            char *clean_desc = strdup(qi->pkg.i->description);
            slapt_clean_description(clean_desc, qi->pkg.i->name);

            if (clean_desc != NULL)
                _pkgtools_status_set_desc(clean_desc);
            _pkgtools_status_set_action(strdup((char *)_("Installing...")));
            _pkgtools_status_set_msg(strdup(qi->pkg.i->name));
            _pkgtools_status_set_total_percent(((count * 100) / trans->queue->size) / 100);

            if (slapt_install_pkg(global_config, qi->pkg.i) == -1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_failed, NULL, NULL);
                return FALSE;
            }
        } else if (qi->type == SLAPT_ACTION_UPGRADE) {
            char *clean_desc = strdup(qi->pkg.u->upgrade->description);
            slapt_clean_description(clean_desc, qi->pkg.u->upgrade->name);

            if (clean_desc != NULL)
                _pkgtools_status_set_desc(clean_desc);
            _pkgtools_status_set_action(strdup((char *)_("Upgrading...")));
            _pkgtools_status_set_msg(strdup(qi->pkg.u->upgrade->name));
            _pkgtools_status_set_total_percent(((count * 100) / trans->queue->size) / 100);

            if (slapt_upgrade_pkg(global_config, qi->pkg.u->upgrade) == -1) {
                g_source_remove(updater);
                gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_failed, NULL, NULL);
                return FALSE;
            }
        }
        ++count;
    }

    g_source_remove(updater);
    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _install_packages_complete, NULL, NULL);
    return TRUE;
}

void clean_callback(GtkWidget *widget, gpointer user_data)
{
    GThread *gdp;

#if !GLIB_CHECK_VERSION(2, 31, 0)
    gdp = g_thread_create((GThreadFunc)slapt_clean_pkg_dir, global_config->working_dir, FALSE, NULL);
#else
    gdp = g_thread_new("GslaptCleanCallback", (GThreadFunc)slapt_clean_pkg_dir, global_config->working_dir);
    g_thread_unref(gdp);
#endif
}

void preferences_sources_add(GtkWidget *w, gpointer user_data)
{
    gslapt_load_ui(gslapt_builder, "source_window.ui");
    GtkWidget *source_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "source_window"));
    gtk_widget_show_all(source_window);
}

void preferences_sources_remove(GtkWidget *w, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeView *source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_sources_treeview"));
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(source_tree));
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(source_tree));

    if (gtk_tree_selection_get_selected(select, &model, &iter)) {
        gtk_list_store_remove(store, &iter);
        sources_modified = TRUE;
    }
}

void preferences_sources_edit(GtkWidget *w, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeView *source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_sources_treeview"));
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(source_tree));

    if (gtk_tree_selection_get_selected(select, &model, &iter)) {
        guint priority;
        gchar *source;

        gtk_tree_model_get(model, &iter, 1, &source, 4, &priority, -1);

        if (source) {
            gslapt_load_ui(gslapt_builder, "source_window.ui");
            GtkWidget *source_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "source_window"));

            GtkEntry *source_entry = GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "source_entry"));
            GtkComboBox *source_priority = GTK_COMBO_BOX(gtk_builder_get_object(gslapt_builder, "source_priority"));

            g_object_set_data(G_OBJECT(source_window), "original_url", source);
            gtk_entry_set_text(source_entry, source);
            gtk_combo_box_set_active(source_priority, convert_slapt_priority_to_gslapt_priority(priority));
            gtk_widget_show_all(source_window);
        }
    }
}

void preferences_on_ok_clicked(GtkWidget *w, gpointer user_data)
{
    GtkEntry *preferences_working_dir_entry = GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "preferences_working_dir_entry"));
    const gchar *working_dir = gtk_entry_get_text(preferences_working_dir_entry);
    GtkTreeModel *model;
    GtkTreeView *tree;
    GtkTreeIter iter;
    gboolean valid;

    strcpy(global_config->working_dir, working_dir);
    slapt_working_dir_init(global_config);
    chdir(global_config->working_dir);

    slapt_vector_t_free(global_config->exclude_list);
    slapt_vector_t_free(global_config->sources);

    global_config->exclude_list = slapt_vector_t_init(free);
    global_config->sources = slapt_vector_t_init((slapt_vector_t_free_function)slapt_source_t_free);

    tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_sources_treeview"));
    model = gtk_tree_view_get_model(tree);
    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *source = NULL;
        gboolean status;
        SLAPT_PRIORITY_T priority;
        gtk_tree_model_get(model, &iter, 1, &source, 2, &status, 4, &priority, -1);

        if (source != NULL) {
            slapt_source_t *src = slapt_source_t_init(source);
            if (src != NULL) {
                if (status)
                    src->disabled = false;
                else
                    src->disabled = true;

                src->priority = priority;

                slapt_vector_t_add(global_config->sources, src);
            }
            g_free(source);
        }

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_exclude_treeview"));
    model = gtk_tree_view_get_model(tree);
    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        gchar *exclude = NULL;
        gtk_tree_model_get(model, &iter, 0, &exclude, -1);

        slapt_vector_t_add(global_config->exclude_list, strdup(exclude));
        g_free(exclude);

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    if (slapt_config_t_write(global_config, rc_location) != 0) {
        notify((gchar *)_("Error"), (gchar *)_("Failed to commit preferences"));
        on_gslapt_destroy(NULL, NULL);
    }

    preferences_window = NULL;
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "window_preferences")));

    /* dialog to resync package sources */
    if (sources_modified == TRUE) {
        sources_modified = FALSE;

        if (excludes_modified == TRUE)
            excludes_modified = FALSE;

        gslapt_load_ui(gslapt_builder, "repositories_changed.ui");
        GtkWidget *rc = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "repositories_changed"));
        gtk_widget_show_all(rc);
    } else {
        /* rebuild package list */
        if (excludes_modified == TRUE) {
            excludes_modified = FALSE;
            rebuild_treeviews(NULL, FALSE);
        }
    }
}

void preferences_exclude_add(GtkWidget *w, gpointer user_data)
{
    GtkEntry *new_exclude_entry = GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "new_exclude_entry"));
    const gchar *new_exclude = gtk_entry_get_text(new_exclude_entry);
    GtkTreeView *exclude_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_exclude_treeview"));
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(exclude_tree));
    GtkTreeIter iter;

    if (new_exclude == NULL || strlen(new_exclude) < 1)
        return;

    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, new_exclude, -1);

    gtk_entry_set_text(new_exclude_entry, "");
    excludes_modified = TRUE;
}

void preferences_exclude_remove(GtkWidget *w, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeView *exclude_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_exclude_treeview"));
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(exclude_tree));
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(exclude_tree));

    if (gtk_tree_selection_get_selected(select, &model, &iter)) {
        gtk_list_store_remove(store, &iter);
        excludes_modified = TRUE;
    }
}

void cancel_preferences(GtkWidget *w, gpointer user_data)
{
    preferences_window = NULL;
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "window_preferences")));
}

void cancel_transaction(GtkWidget *w, gpointer user_data)
{
    unset_busy_cursor();
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "transaction_window")));
}

void add_pkg_for_reinstall(GtkWidget *gslapt, gpointer user_data)
{
    global_config->re_install = TRUE;
    add_pkg_for_install(gslapt, user_data);
    global_config->re_install = FALSE;
}

static void set_execute_active(void)
{
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_execute_button")), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "execute1")), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark_all1")), TRUE);
    gslapt_set_status((gchar *)_("Pending changes. Click execute when ready."));
}

static void clear_execute_active(void)
{
    gslapt_clear_status();
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_execute_button")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "execute1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark_all1")), FALSE);
}

static void notify(const char *title, const char *message)
{
    gslapt_load_ui(gslapt_builder, "notification.ui");
    GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "notification"));
    gtk_window_set_transient_for(GTK_WINDOW(w), GTK_WINDOW(gslapt));
    if(!(gslapt_settings->notify_width == 0 && gslapt_settings->notify_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(w), gslapt_settings->notify_width, gslapt_settings->notify_height);
    if (!(gslapt_settings->notify_x == 0 && gslapt_settings->notify_y == 0))
        gtk_window_move(GTK_WINDOW(w), gslapt_settings->notify_x, gslapt_settings->notify_y);

    gtk_window_set_title(GTK_WINDOW(w), title);
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "notification_label")), message);
    gtk_label_set_use_markup(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "notification_label")), TRUE);
    gtk_widget_show_all(w);
}

static gboolean pkg_action_popup_menu(GtkTreeView *treeview, gpointer data)
{
    GdkEventButton *eventb = (GdkEventButton *)gtk_get_current_event();
    GdkEvent *event = gtk_get_current_event();

    if (event == NULL || eventb == NULL)
        return FALSE;

    if (eventb->type != GDK_BUTTON_PRESS)
        return FALSE;

    GtkTreeViewColumn *column;
    GtkTreePath *path;
    if (!gtk_tree_view_get_path_at_pos(treeview, eventb->x, eventb->y, &path, &column, NULL, NULL)) {
        gtk_tree_path_free(path);
        return FALSE;
    }
    gtk_tree_path_free(path);
    if (eventb->button != 3 && (eventb->button == 1 && strcmp(gtk_tree_view_column_get_title(column), (gchar *)_("Status")) != 0))
        return FALSE;

    GtkMenu *menu = GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "package1"))));
#if GTK_CHECK_VERSION(3,22,0)
    gtk_menu_popup_at_pointer(menu, event);
#else
    gtk_menu_popup(
        menu,
        NULL,
        NULL,
        NULL,
        NULL,
        eventb->button,
        gtk_get_current_event_time());
#endif

    return TRUE;
}

void unmark_package(GtkWidget *gslapt, gpointer user_data)
{
    GtkTreeView *treeview;
    GtkTreeIter iter;
    GtkTreeSelection *selection;
    slapt_pkg_t *pkg = NULL;
    guint is_installed = 0;
    GtkTreeModelSort *package_model;

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    selection = gtk_tree_view_get_selection(treeview);
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    if (gtk_tree_selection_get_selected(selection, (GtkTreeModel **)&package_model, &iter) == TRUE) {
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
            fprintf(stderr, "failed to get package name and version from selection\n");

            if (pkg_name != NULL)
                g_free(pkg_name);

            if (pkg_version != NULL)
                g_free(pkg_version);

            if (pkg_location != NULL)
                g_free(pkg_location);

            return;
        }

        if (slapt_get_exact_pkg(installed, pkg_name, pkg_version) != NULL)
            is_installed = 1;

        if (((pkg = slapt_get_pkg_by_details(all, pkg_name, pkg_version, pkg_location)) == NULL)) {
            pkg = slapt_get_exact_pkg(installed, pkg_name, pkg_version);
        }

        if (pkg == NULL) {
            fprintf(stderr, "Failed to find package: %s-%s@%s\n", pkg_name, pkg_version, pkg_location);
            g_free(pkg_name);
            g_free(pkg_version);
            g_free(pkg_location);
            return;
        }
        g_free(pkg_name);
        g_free(pkg_version);
        g_free(pkg_location);

        /* convert sort model and iter to filter */
        gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(package_model), &filter_iter, &iter);
        filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
        /* convert filter to regular tree */
        gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model), &actual_iter, &filter_iter);
        model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

        if (is_installed == 1) {
            GdkPixbuf *status_icon = gslapt_img("pkg_action_installed.png");
            gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, STATUS_ICON_COLUMN, status_icon, -1);
            status = g_strdup_printf("a%s", pkg->name);
            g_object_unref(status_icon);
        } else {
            GdkPixbuf *status_icon = gslapt_img("pkg_action_available.png");
            gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, STATUS_ICON_COLUMN, status_icon, -1);
            status = g_strdup_printf("z%s", pkg->name);
            g_object_unref(status_icon);
        }
        gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, STATUS_COLUMN, status, -1);
        g_free(status);

        gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, MARKED_COLUMN, FALSE, -1);

        /* clear the installed version as well if this was an upgrade */
        slapt_vector_t_foreach (slapt_pkg_upgrade_t *, upgrade, trans->upgrade_pkgs) {
            if (strcmp(upgrade->installed->name, pkg->name) == 0) {
                slapt_pkg_t *installed_pkg = upgrade->installed;
                slapt_pkg_t *upgrade_pkg = upgrade->upgrade;

                if (installed_pkg == NULL)
                    continue;

                if (set_iter_to_pkg(model, &actual_iter, installed_pkg)) {
                    gchar *istatus = g_strdup_printf("i%s", installed_pkg->name);
                    GdkPixbuf *status_icon = gslapt_img("pkg_action_installed.png");
                    gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, STATUS_ICON_COLUMN, status_icon, -1);
                    gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, STATUS_COLUMN, istatus, -1);
                    gtk_list_store_set(GTK_LIST_STORE(model), &actual_iter, MARKED_COLUMN, FALSE, -1);
                    g_free(istatus);
                    g_object_unref(status_icon);
                } else {
                    fprintf(stderr, "failed to find iter for installed package %s-%s to unmark\n", upgrade->installed->name, upgrade->installed->version);
                }

                if (upgrade_pkg != NULL) {
                    trans = slapt_remove_from_transaction(trans, upgrade_pkg);
                }
            }
        }

        trans = slapt_remove_from_transaction(trans, pkg);
        if (trans->install_pkgs->size == 0 &&
            trans->remove_pkgs->size == 0 &&
            trans->upgrade_pkgs->size == 0 &&
            trans->reinstall_pkgs->size == 0) {
            clear_execute_active();
        }
    }

    rebuild_package_action_menu();
}

/* parse the dependencies for a package, and add them to the transaction as */
/* needed check to see if a package is conflicted */
static int ladd_deps_to_trans(slapt_transaction_t *tran, slapt_vector_t *avail_pkgs, slapt_vector_t *installed_pkgs, slapt_pkg_t *pkg)
{
    int dep_return = -1;
    slapt_vector_t *deps = NULL;
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;

    if (global_config->disable_dep_check == TRUE)
        return 0;
    if (pkg == NULL)
        return 0;

    deps = slapt_vector_t_init(NULL);

    dep_return = slapt_get_pkg_dependencies(global_config, avail_pkgs, installed_pkgs, pkg, deps, tran->conflict_err, tran->missing_err);

    /* check to see if there where issues with dep checking */
    /* exclude the package if dep check barfed */
    if ((dep_return == -1) && (global_config->ignore_dep == FALSE) && (slapt_get_exact_pkg(tran->exclude_pkgs, pkg->name, pkg->version) == NULL)) {
        /* don't add exclude here... later we offer an option to install anyway */
        /* slapt_add_exclude_to_transaction(tran,pkg); */
        slapt_vector_t_free(deps);
        return -1;
    }

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    /* loop through the deps */
    slapt_vector_t_foreach (slapt_pkg_t *, dep, deps) {
        slapt_pkg_t *dep_installed;
        slapt_vector_t *conflicts = slapt_is_conflicted(tran, avail_pkgs, installed_pkgs, dep);

        /* the dep wouldn't get this far if it where excluded, so we don't check for that here */

        if (conflicts->size > 1) {
            slapt_vector_t_foreach (slapt_pkg_t *, conflicted_pkg, conflicts) {
                slapt_add_remove_to_transaction(tran, conflicted_pkg);
                if (set_iter_to_pkg(GTK_TREE_MODEL(base_model), &iter, conflicted_pkg)) {
                    set_iter_for_remove(base_model, &iter, conflicted_pkg);
                }
            }
        }

        dep_installed = slapt_get_newest_pkg(installed_pkgs, dep->name);
        if (dep_installed == NULL) {
            slapt_add_install_to_transaction(tran, dep);
            if (set_iter_to_pkg(GTK_TREE_MODEL(base_model), &iter, dep)) {
                set_iter_for_install(base_model, &iter, dep);
            }
        } else {
            /* add only if its a valid upgrade */
            if (slapt_cmp_pkgs(dep_installed, dep) < 0) {
                slapt_add_upgrade_to_transaction(tran, dep_installed, dep);
                if (set_iter_to_pkg(GTK_TREE_MODEL(base_model), &iter, dep)) {
                    set_iter_for_upgrade(base_model, &iter, dep);
                }
            }
        }
    }

    slapt_vector_t_free(deps);

    return 0;
}

static int set_iter_to_pkg(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(model, iter);
    while (valid) {
        gchar *name, *version, *location;
        gtk_tree_model_get(model, iter, NAME_COLUMN, &name, VERSION_COLUMN, &version, LOCATION_COLUMN, &location, -1);

        if (name == NULL || version == NULL || location == NULL) {
            valid = gtk_tree_model_iter_next(model, iter);
            continue;
        }

        if (strcmp(name, pkg->name) == 0 &&
            strcmp(version, pkg->version) == 0 &&
            strcmp(location, pkg->location) == 0) {
            g_free(name);
            g_free(version);
            g_free(location);
            return 1;
        }
        g_free(name);
        g_free(version);
        g_free(location);

        valid = gtk_tree_model_iter_next(model, iter);
    }
    return 0;
}

void build_treeview_columns(GtkWidget *treeview)
{
    GtkTreeSelection *select;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    /* column for installed status */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Status"), renderer, "pixbuf", STATUS_ICON_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, STATUS_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* column for name */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Name"), renderer, "text", NAME_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, NAME_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* column for version */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Version"), renderer, "text", VERSION_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, VERSION_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* column for series */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Location"), renderer, "text", SERIES_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, SERIES_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* column for short description */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Description"), renderer, "text", DESC_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, DESC_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* column for installed size */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Installed Size"), renderer, "text", SIZE_COLUMN, NULL);
    gtk_tree_view_column_set_sort_column_id(column, SIZE_COLUMN);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_resizable(column, TRUE);

    /* invisible column to sort installed by */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Installed"), renderer, "text", STATUS_COLUMN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_visible(column, FALSE);

    /* column to set visibility */
    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Visible"), renderer, "radio", VISIBLE_COLUMN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    gtk_tree_view_column_set_visible(column, FALSE);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(select), "changed", G_CALLBACK(show_pkg_details), NULL);

    g_signal_connect(G_OBJECT(treeview), "cursor-changed", G_CALLBACK(pkg_action_popup_menu), NULL);
}

static slapt_pkg_upgrade_t *lsearch_upgrade_transaction(slapt_transaction_t *tran, slapt_pkg_t *pkg)
{
    /*
    lookup the package in the upgrade list, checking for the same name,version
    or location (in the case of location, either the same location for upgrade
    or installed)
    */
    slapt_vector_t_foreach (slapt_pkg_upgrade_t *, upgrade, tran->upgrade_pkgs) {
        if (strcmp(pkg->name, upgrade->upgrade->name) == 0 && strcmp(pkg->version, upgrade->upgrade->version) == 0 &&
           (strcmp(pkg->location, upgrade->upgrade->location) == 0 || strcmp(pkg->location, upgrade->installed->location) == 0)) {
            return upgrade;
        }
    }

    return NULL;
}

void open_icon_legend(GObject *object, gpointer user_data)
{
    gslapt_load_ui(gslapt_builder, "icon_legend.ui");
    GtkWidget *icon_legend = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "icon_legend"));
    gtk_window_set_transient_for(GTK_WINDOW(icon_legend), GTK_WINDOW(gslapt));
    gtk_widget_show_all(icon_legend);
}

void on_button_cancel_clicked(GtkButton *button, gpointer user_data)
{
    G_LOCK(_cancelled);
    _cancelled = 1;
    G_UNLOCK(_cancelled);
}

static void build_package_action_menu(slapt_pkg_t *pkg)
{
    slapt_pkg_t *newest_installed = NULL, *upgrade_pkg = NULL;
    guint is_installed = 0, is_newest = 1, is_exclude = 0, is_downloadable = 0, is_downgrade = 0;

    if (slapt_get_exact_pkg(installed, pkg->name, pkg->version) != NULL)
        is_installed = 1;

    /* in order to know if a package is re-installable, we have to just look at the name and version, not the location */
    if (slapt_get_exact_pkg(all, pkg->name, pkg->version) != NULL)
        is_downloadable = 1;

    /* find out if there is a newer available package */
    upgrade_pkg = slapt_get_newest_pkg(all, pkg->name);
    if (upgrade_pkg != NULL && slapt_cmp_pkgs(pkg, upgrade_pkg) < 0)
        is_newest = 0;

    if ((newest_installed = slapt_get_newest_pkg(installed, pkg->name)) != NULL) {
        /* this will always be true for high priority package sources */
        if (slapt_cmp_pkgs(pkg, newest_installed) < 0) {
            is_downgrade = 1;
        } else if (is_newest == 0 && slapt_cmp_pkg_versions(pkg->version, newest_installed->version) < 0) {
            /* if pkg is not the newest available version and the version is actually 
            less (and does not have a higher priority or would have been handled
            above) then we consider this a downgrade */
            is_downgrade = 1;
        } else if (slapt_cmp_pkgs(pkg, newest_installed) == 0) {
            /* maybe this isn't the exact installed package, but it's different enough
            to warrant reinstall-ability... it is questionable if this is ever
            reached */
            is_installed = 1;
        }
    }

    if (slapt_is_excluded(global_config, pkg) == 1 || slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) != NULL) {
        is_exclude = 1;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "upgrade1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "re-install1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "downgrade1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "install1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "remove1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark1")), FALSE);

    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "upgrade1")), add_pkg_for_install, G_OBJECT(gslapt));
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "re-install1")), add_pkg_for_reinstall, G_OBJECT(gslapt));
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "downgrade1")), add_pkg_for_reinstall, G_OBJECT(gslapt));
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "install1")), add_pkg_for_install, G_OBJECT(gslapt));
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "remove1")), add_pkg_for_removal, G_OBJECT(gslapt));
    g_signal_handlers_disconnect_by_func(G_OBJECT(gtk_builder_get_object(gslapt_builder, "unmark1")), unmark_package, G_OBJECT(gslapt));

    g_signal_connect_swapped(G_OBJECT(gtk_builder_get_object(gslapt_builder, "upgrade1")), "activate", G_CALLBACK(add_pkg_for_install), GTK_WIDGET(gslapt));
    g_signal_connect_swapped((gpointer)gtk_builder_get_object(gslapt_builder, "re-install1"), "activate", G_CALLBACK(add_pkg_for_reinstall), G_OBJECT(gslapt));
    g_signal_connect_swapped((gpointer)gtk_builder_get_object(gslapt_builder, "downgrade1"), "activate", G_CALLBACK(add_pkg_for_reinstall), G_OBJECT(gslapt));
    g_signal_connect_swapped(G_OBJECT(gtk_builder_get_object(gslapt_builder, "install1")), "activate", G_CALLBACK(add_pkg_for_install), GTK_WIDGET(gslapt));
    g_signal_connect_swapped(G_OBJECT(gtk_builder_get_object(gslapt_builder, "remove1")), "activate", G_CALLBACK(add_pkg_for_removal), GTK_WIDGET(gslapt));
    g_signal_connect_swapped(G_OBJECT(gtk_builder_get_object(gslapt_builder, "unmark1")), "activate", G_CALLBACK(unmark_package), GTK_WIDGET(gslapt));

    if (slapt_search_transaction(trans, pkg->name) == 0) {
        if (is_exclude == 0) {
            /* upgrade */
            if (is_installed == 1 && is_newest == 0 && (slapt_search_transaction_by_pkg(trans, upgrade_pkg) == 0)) {
                gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "upgrade1")), TRUE);
                /* re-install */
            } else if (is_installed == 1 && is_newest == 1 && is_downloadable == 1) {
                gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "re-install1")), TRUE);
                /* this is for downgrades */
            } else if (is_installed == 0 && is_downgrade == 1 && is_downloadable == 1) {
                gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "downgrade1")), TRUE);
                /* straight up install */
            } else if (is_installed == 0 && is_downloadable == 1) {
                gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "install1")), TRUE);
            }
        }
    } else {
        if (slapt_get_exact_pkg(trans->exclude_pkgs, pkg->name, pkg->version) == NULL) {
            gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark1")), TRUE);
        }
    }

    if (is_installed == 1 && is_exclude != 1 && (slapt_search_transaction(trans, pkg->name) == 0)) {
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "remove1")), TRUE);
    }
}

static void rebuild_package_action_menu(void)
{
    GtkTreeView *treeview;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkTreeModelSort *package_model;

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));
    selection = gtk_tree_view_get_selection(treeview);

    if (gtk_tree_selection_get_selected(selection, (GtkTreeModel **)&package_model, &iter) == TRUE) {
        gchar *pkg_name;
        gchar *pkg_version;
        gchar *pkg_location;
        slapt_pkg_t *pkg = NULL;

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

        pkg = slapt_get_pkg_by_details(all, pkg_name, pkg_version, pkg_location);
        if (pkg == NULL) {
            pkg = slapt_get_pkg_by_details(installed, pkg_name, pkg_version, pkg_location);
        }
        g_free(pkg_name);
        g_free(pkg_version);
        g_free(pkg_location);

        if (pkg != NULL) {
            build_package_action_menu(pkg);
        }

    } else {
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "upgrade1")), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "re-install1")), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "downgrade1")), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "install1")), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "remove1")), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark1")), FALSE);
    }
}

void unmark_all_activate(GtkMenuItem *menuitem, gpointer user_data)
{
    set_busy_cursor();
    lock_toolbar_buttons();

    /* reset our currently selected packages */
    slapt_free_transaction(trans);
    trans = slapt_init_transaction();

    rebuild_package_action_menu();
    rebuild_treeviews(NULL, FALSE);
    unlock_toolbar_buttons();
    clear_execute_active();
    unset_busy_cursor();
}

static void reset_search_list(void)
{
    gboolean view_list_all = FALSE, view_list_installed = FALSE,
             view_list_available = FALSE, view_list_marked = FALSE,
             view_list_upgradeable = FALSE;

    view_list_all = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_all_packages_menu")));
    view_list_available = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_available_packages_menu")));
    view_list_installed = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_installed_packages_menu")));
    view_list_marked = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_marked_packages_menu")));
    view_list_upgradeable = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(gtk_builder_get_object(gslapt_builder, "view_upgradeable_packages_menu")));

    if (view_list_all) {
        view_all_packages(NULL, NULL);
    } else if (view_list_installed) {
        view_installed_packages(NULL, NULL);
    } else if (view_list_available) {
        view_available_packages(NULL, NULL);
    } else if (view_list_marked) {
        view_marked_packages(NULL, NULL);
    } else if (view_list_upgradeable) {
        view_upgradeable_packages(NULL, NULL);
    }
}

GtkEntryCompletion *build_search_completions(void)
{
    GtkTreeModel *completions;
    GtkEntryCompletion *completion;

    completions = GTK_TREE_MODEL(gtk_list_store_new(1, G_TYPE_STRING));

    completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion, completions);
    g_object_unref(completions);
    gtk_entry_completion_set_text_column(completion, 0);

    return completion;
}

void repositories_changed_callback(GtkWidget *repositories_changed, gpointer user_data)
{
    gtk_widget_destroy(GTK_WIDGET(repositories_changed));
    g_signal_emit_by_name(gtk_builder_get_object(gslapt_builder, "action_bar_update_button"), "clicked");
}

void update_activate(GtkMenuItem *menuitem, gpointer user_data)
{
    return update_callback(NULL, NULL);
}

void mark_all_upgrades_activate(GtkMenuItem *menuitem, gpointer user_data)
{
    return upgrade_callback(NULL, NULL);
}

void execute_activate(GtkMenuItem *menuitem, gpointer user_data)
{
    return execute_callback(NULL, NULL);
}

slapt_vector_t *parse_disabled_package_sources(const char *file_name)
{
    FILE *rc = NULL;
    char *getline_buffer = NULL;
    size_t gb_length = 0;
    ssize_t g_size;
    slapt_vector_t *list = slapt_vector_t_init((slapt_vector_t_free_function)slapt_source_t_free);

    rc = slapt_open_file(file_name, "r");
    if (rc == NULL)
        return list;

    while ((g_size = getline(&getline_buffer, &gb_length, rc)) != EOF) {
        char *nl = NULL;
        if ((nl = strchr(getline_buffer, '\n')) != NULL) {
            nl[0] = '\0';
        }

        if (strstr(getline_buffer, "#DISABLED=") != NULL) {
            if (g_size > 10) {
                slapt_source_t *src = slapt_source_t_init(getline_buffer + 10);
                if (src != NULL)
                    slapt_vector_t_add(list, src);
            }
        }
    }
    if (getline_buffer)
        free(getline_buffer);

    fclose(rc);

    return list;
}

static gboolean toggle_source_status(GtkTreeView *treeview, gpointer data)
{
    GdkEventButton *event = (GdkEventButton *)gtk_get_current_event();
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    GtkTreePath *path;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (event == NULL)
        return FALSE;

    if (event->type != GDK_BUTTON_PRESS)
        return FALSE;

    if (!gtk_tree_view_get_path_at_pos(treeview, event->x, event->y, &path, &column, NULL, NULL))
        return FALSE;

    if (strcmp(gtk_tree_view_column_get_title(column), (gchar *)_("Enabled")) != 0)
        return FALSE;

    gtk_tree_path_free(path);

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *source;
        gboolean status;

        gtk_tree_model_get(model, &iter, 1, &source, 2, &status, -1);

        if (status) { /* is active */
            GdkPixbuf *status_icon = gslapt_img("pkg_action_available.png");
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, status_icon, 2, FALSE, -1);
            g_object_unref(status_icon);
        } else { /* is not active */
            GdkPixbuf *status_icon = gslapt_img("pkg_action_installed.png");
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, status_icon, 2, TRUE, -1);
            g_object_unref(status_icon);
        }

        sources_modified = TRUE;
        g_free(source);
    }

    return FALSE;
}

static void display_dep_error_dialog(slapt_pkg_t *pkg)
{
    GtkTextBuffer *error_buf = NULL;
    gchar *msg = g_strdup_printf((gchar *)_("<b>Excluding %s due to dependency failure</b>"), pkg->name);

    gslapt_load_ui(gslapt_builder, "dep_error_dialog.ui");
    GtkWidget *w = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "dep_error_dialog"));

    gtk_window_set_title(GTK_WINDOW(w), (char *)_("Error"));
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dep_error_label")), msg);
    g_free(msg);

    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dep_error_install_anyway_warning_label")),
                       (char *)_("Missing dependencies may mean the software in this package will not function correctly.  Do you want to continue without the required packages?"));

    gtk_label_set_use_markup(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dep_error_label")), TRUE);
    gtk_label_set_use_markup(GTK_LABEL(gtk_builder_get_object(gslapt_builder, "dep_error_install_anyway_warning_label")), TRUE);
    error_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(gslapt_builder, "dep_error_text")));

    slapt_vector_t_foreach (slapt_pkg_err_t *, missing_err, trans->missing_err) {
        unsigned int len = strlen(missing_err->pkg) + strlen((gchar *)_(": Depends: ")) + strlen(missing_err->error) + 2;
        char *err = slapt_malloc(sizeof *err * len);
        snprintf(err, len, "%s: Depends: %s\n", missing_err->pkg, missing_err->error);
        gtk_text_buffer_insert_at_cursor(error_buf, err, -1);
        free(err);
    }

    slapt_vector_t_foreach (slapt_pkg_err_t *, conflict_err, trans->conflict_err) {
        unsigned int len = strlen(conflict_err->error) + strlen((gchar *)_(", which is required by ")) + strlen(conflict_err->pkg) + strlen((gchar *)_(", is excluded")) + 2;
        char *err = slapt_malloc(sizeof *err * len);
        snprintf(err, len, "%s, which is required by %s, is excluded\n", conflict_err->error, conflict_err->pkg);
        gtk_text_buffer_insert_at_cursor(error_buf, err, -1);
        free(err);
    }

    gtk_widget_show_all(w);
    gint result = gtk_dialog_run(GTK_DIALOG(w));
    if (result == GTK_RESPONSE_OK) {
        GtkTreeIter iter;
        slapt_pkg_t *installed_pkg = slapt_get_newest_pkg(installed, pkg->name);

        GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
        GtkTreeModelSort *package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));
        GtkTreeModelFilter *filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
        GtkTreeModel *model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));
        if (installed_pkg == NULL) {
            slapt_add_install_to_transaction(trans, pkg);
            set_iter_for_install(model, &iter, pkg);
        } else {
            int ver_cmp = slapt_cmp_pkgs(installed_pkg, pkg);
            slapt_add_upgrade_to_transaction(trans, installed_pkg, pkg);
            if (ver_cmp == 0) {
                set_iter_for_reinstall(model, &iter, pkg);
            } else if (ver_cmp > 1) {
                set_iter_for_upgrade(model, &iter, pkg);
            } else if (ver_cmp < 1) {
                set_iter_for_downgrade(model, &iter, pkg);
            }
        }
        set_execute_active();
    } else if (result == GTK_RESPONSE_CANCEL) {
        slapt_add_exclude_to_transaction(trans, pkg);
    }

    rebuild_package_action_menu();
    gtk_widget_destroy(w);
}

void view_all_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeIter iter;
    gboolean valid;
    GtkTreeModelSort *package_model;

    if (pattern && strlen(pattern) > 0)
        return build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    valid = gtk_tree_model_get_iter_first(base_model, &iter);
    while (valid) {
        gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        valid = gtk_tree_model_iter_next(base_model, &iter);
    }
}

void view_available_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    gboolean show_installed = FALSE, show_available = TRUE;
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));

    view_installed_or_available_packages(show_installed, show_available);

    if (pattern && strlen(pattern) > 0)
        build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_installed_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    gboolean show_installed = TRUE, show_available = FALSE;
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));
    GtkTreeView *treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));

    view_installed_or_available_packages(show_installed, show_available);

    if (pattern && strlen(pattern) > 0)
        build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_installed_or_available_packages(gboolean show_installed, gboolean show_available)
{
    gboolean valid;
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    valid = gtk_tree_model_get_iter_first(base_model, &iter);
    while (valid) {
        gboolean from_installed_set = FALSE;

        gtk_tree_model_get(base_model, &iter, INST_COLUMN, &from_installed_set, -1);

        if (from_installed_set && show_installed) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else if (!from_installed_set && show_available) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, FALSE, -1);
        }

        valid = gtk_tree_model_iter_next(base_model, &iter);
    }
}

void view_marked_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    gboolean valid;
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    valid = gtk_tree_model_get_iter_first(base_model, &iter);
    while (valid) {
        gboolean marked = FALSE;

        gtk_tree_model_get(base_model, &iter, MARKED_COLUMN, &marked, -1);

        if (marked) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, FALSE, -1);
        }

        valid = gtk_tree_model_iter_next(base_model, &iter);
    }

    if (pattern && strlen(pattern) > 0)
        build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

static int set_iter_for_install(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gchar *status = g_strdup_printf("i%s", pkg->name);
    GdkPixbuf *status_icon = gslapt_img("pkg_action_install.png");
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_ICON_COLUMN, status_icon, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_COLUMN, status, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MARKED_COLUMN, TRUE, -1);
    g_free(status);
    g_object_unref(status_icon);
    return 0;
}

static int set_iter_for_reinstall(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gchar *status = g_strdup_printf("u%s", pkg->name);
    GdkPixbuf *status_icon = gslapt_img("pkg_action_reinstall.png");
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_ICON_COLUMN, status_icon, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_COLUMN, status, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MARKED_COLUMN, TRUE, -1);
    g_free(status);
    g_object_unref(status_icon);
    return 0;
}

static int set_iter_for_downgrade(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gchar *status = g_strdup_printf("u%s", pkg->name);
    GdkPixbuf *status_icon = gslapt_img("pkg_action_downgrade.png");
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_ICON_COLUMN, status_icon, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_COLUMN, status, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MARKED_COLUMN, TRUE, -1);
    g_free(status);
    g_object_unref(status_icon);
    return 0;
}

static int set_iter_for_upgrade(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gchar *status = g_strdup_printf("u%s", pkg->name);
    GdkPixbuf *status_icon = gslapt_img("pkg_action_upgrade.png");
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_ICON_COLUMN, status_icon, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_COLUMN, status, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MARKED_COLUMN, TRUE, -1);
    g_free(status);
    g_object_unref(status_icon);
    return 0;
}

static int set_iter_for_remove(GtkTreeModel *model, GtkTreeIter *iter, slapt_pkg_t *pkg)
{
    gchar *status = g_strdup_printf("r%s", pkg->name);
    GdkPixbuf *status_icon = gslapt_img("pkg_action_remove.png");
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_ICON_COLUMN, status_icon, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, STATUS_COLUMN, status, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, MARKED_COLUMN, TRUE, -1);
    g_free(status);
    g_object_unref(status_icon);
    return 0;
}

void mark_obsolete_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;

    set_busy_cursor();

    slapt_vector_t *obsolete = slapt_get_obsolete_pkgs(global_config, all, installed);

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(treeview));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    slapt_vector_t_foreach (slapt_pkg_t *, obsolete_pkg, obsolete) {
        if (slapt_is_excluded(global_config, obsolete_pkg) == 1) {
            slapt_add_exclude_to_transaction(trans, obsolete_pkg);
        } else {
            slapt_add_remove_to_transaction(trans, obsolete_pkg);
            set_iter_to_pkg(base_model, &iter, obsolete_pkg);
            set_iter_for_remove(base_model, &iter, obsolete_pkg);
            set_execute_active();
        }
    }

    unset_busy_cursor();
    slapt_vector_t_free(obsolete);
}

static void set_busy_cursor(void)
{
    GdkDisplay *display = gtk_widget_get_display(gslapt);
    GdkCursor *c = gdk_cursor_new_for_display(display, GDK_WATCH);
    gdk_window_set_cursor(gtk_widget_get_window(gslapt), c);
    gdk_display_flush(display);
#if GTK_CHECK_VERSION(3,0,0)
    g_object_unref(c);
#else
    gdk_cursor_unref(c);
#endif
}

static void unset_busy_cursor(void)
{
    gdk_window_set_cursor(gtk_widget_get_window(gslapt), NULL);
    GdkDisplay *display = gtk_widget_get_display(gslapt);
    gdk_display_flush(display);
    //gdk_flush();
}

static void build_verification_sources_treeviewlist(GtkWidget *treeview)
{
    GtkListStore *store;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;

    store = gtk_list_store_new(1, G_TYPE_STRING);

    slapt_vector_t_foreach (slapt_source_t *, src, global_config->sources) {
        if (src->url == NULL)
            continue;

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, src->url, -1);
    }

    /* column for url */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes((gchar *)_("Source"), renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sort_column_id(column, 0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
    g_object_unref(store);
    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
}

static int _get_gpg_key_init(gpointer data)
{
    g_mutex_lock(&progress_window_mutex);
    gslapt_load_ui(gslapt_builder, "dl_progress_window.ui");
    progress_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "dl_progress_window"));
    gtk_window_set_transient_for(GTK_WINDOW(progress_window), GTK_WINDOW(gslapt));
    gtk_window_set_title(GTK_WINDOW(progress_window), (gchar *)_("Progress"));
    if(!(gslapt_settings->progress_width == 0 && gslapt_settings->progress_height == 0))
        gtk_window_set_default_size(GTK_WINDOW(progress_window), gslapt_settings->progress_width, gslapt_settings->progress_height);
    if (!(gslapt_settings->progress_x == 0 && gslapt_settings->progress_y == 0))
        gtk_window_move(GTK_WINDOW(progress_window), gslapt_settings->progress_x, gslapt_settings->progress_y);
    gtk_widget_show_all(progress_window);
    g_mutex_unlock(&progress_window_mutex);

    return FALSE;
}

static int _get_gpg_key_error(gpointer data)
{
    gtk_widget_destroy(progress_window);
    progress_window = NULL;
    notify(_("Import"), _("No key found"));
    return FALSE;
}
static int _get_gpg_key_complete(gpointer data)
{
    slapt_code_t result = (slapt_code_t)data;;
    gtk_widget_destroy(progress_window);
    progress_window = NULL;
    notify(_("Import"), slapt_strerror(result));
    return FALSE;
}

#ifdef SLAPT_HAS_GPGME
static void get_gpg_key(gpointer data)
{
    gchar *url = data;
    bool compressed = false;
    FILE *gpg_key = NULL;

    gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_gpg_key_init, NULL, NULL);
    guint updater = gdk_threads_add_timeout_full(G_PRIORITY_HIGH, 20, _update_progress, NULL, NULL);

    _progress_set_total_percent(0.0);
    _progress_set_download_percent(0.0);
    _progress_set_msg(strdup(url));
    _progress_set_action(strdup((char *)SLAPT_GPG_KEY));
    _progress_set_speed(NULL);
    _progress_set_desc(NULL);

    gpg_key = slapt_get_pkg_source_gpg_key(global_config, url, &compressed);

    if (_cancelled == 1) {
        G_LOCK(_cancelled);
        _cancelled = 0;
        G_UNLOCK(_cancelled);
        if (gpg_key) {
            fclose(gpg_key);
            gpg_key = NULL;
        }
    }

    g_source_remove(updater);
    g_free(url);

    if (gpg_key) {
        slapt_code_t result = slapt_add_pkg_source_gpg_key(gpg_key);
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_gpg_key_complete, (void *)result, NULL);
    } else {
        gdk_threads_add_idle_full(G_PRIORITY_HIGH, _get_gpg_key_error, NULL, NULL);
    }
}

void preferences_sources_add_key(GtkWidget *w, gpointer user_data)
{
    GtkTreeView *source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_verification_sources_treeview"));
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(source_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (gtk_tree_selection_get_selected(select, &model, &iter)) {
        gchar *url;
        gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &url, -1);
#if !GLIB_CHECK_VERSION(2, 31, 0)
        GThread *gdp = g_thread_create((GThreadFunc)get_gpg_key, strdup(url), FALSE, NULL);
#else
        GThread *gdp = g_thread_new("GslaptPrefAddSource", (GThreadFunc)get_gpg_key, strdup(url));
        g_thread_unref(gdp);
#endif
    }

    return;
}
#endif

void view_upgradeable_packages(GtkMenuItem *menuitem, gpointer user_data)
{
    gboolean valid;
    GtkTreeIter iter;
    GtkTreeModelFilter *filter_model;
    GtkTreeModel *base_model;
    GtkTreeModelSort *package_model;
    GtkTreeView *treeview;
    gchar *pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")));

    treeview = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview"));
    package_model = GTK_TREE_MODEL_SORT(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));

    filter_model = GTK_TREE_MODEL_FILTER(gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(package_model)));
    base_model = GTK_TREE_MODEL(gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(filter_model)));

    valid = gtk_tree_model_get_iter_first(base_model, &iter);
    while (valid) {
        gboolean upgradeable = FALSE;

        gtk_tree_model_get(base_model, &iter, UPGRADEABLE_COLUMN, &upgradeable, -1);

        if (upgradeable) {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, TRUE, -1);
        } else {
            gtk_list_store_set(GTK_LIST_STORE(base_model), &iter, VISIBLE_COLUMN, FALSE, -1);
        }

        valid = gtk_tree_model_iter_next(base_model, &iter);
    }

    if (pattern && strlen(pattern) > 0)
        build_searched_treeviewlist(GTK_WIDGET(treeview), pattern);
}

void view_changelogs(GtkMenuItem *menuitem, gpointer user_data)
{
    int changelogs = 0;

    gslapt_load_ui(gslapt_builder, "changelog_window.ui");
    GtkWidget *changelog_window = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "changelog_window"));
    gtk_window_set_transient_for(GTK_WINDOW(changelog_window), GTK_WINDOW(gslapt));

    GtkWidget *changelog_notebook = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "changelog_notebook"));

    if(!(gslapt_settings->cl_width == 0 && gslapt_settings->cl_height == 0))
       gtk_window_set_default_size(GTK_WINDOW(changelog_window), gslapt_settings->cl_width, gslapt_settings->cl_height);
    if (!(gslapt_settings->cl_x == 0 && gslapt_settings->cl_y == 0))
       gtk_window_move(GTK_WINDOW(changelog_window), gslapt_settings->cl_x, gslapt_settings->cl_y);

    slapt_vector_t_foreach (slapt_source_t *, src, global_config->sources) {
        char *changelog_filename, *changelog_data;
        gchar *source_url, *path_and_file, *changelog_txt;
        struct stat stat_buf;
        size_t pls = 1;
        FILE *changelog_f = NULL;
        GtkWidget *textview, *scrolledwindow, *label;
        GtkTextBuffer *changelog_buffer;

        if (src->url == NULL)
            continue;
        if (src->disabled == true)
            continue;

        source_url = g_strdup(src->url);

        changelog_filename = slapt_gen_filename_from_url(source_url, SLAPT_CHANGELOG_FILE);
        path_and_file = g_strjoin("/", global_config->working_dir, changelog_filename, NULL);

        if ((changelog_f = fopen(path_and_file, "rb")) == NULL) {
            free(changelog_filename);
            g_free(path_and_file);
            g_free(source_url);
            continue;
        }

        if (stat(changelog_filename, &stat_buf) == -1) {
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

        changelog_data = (char *)mmap(0, pls, PROT_READ | PROT_WRITE, MAP_PRIVATE, fileno(changelog_f), 0);

        fclose(changelog_f);

        if (changelog_data == (void *)-1) {
            g_free(source_url);
            continue;
        }

        changelog_data[pls - 1] = '\0';

        changelog_txt = g_strdup(changelog_data);

        /* munmap now that we are done */
        if (munmap(changelog_data, pls) == -1) {
            g_free(changelog_txt);
            g_free(source_url);
            continue;
        }

        scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
        textview = gtk_text_view_new();
        label = gtk_label_new(source_url);

        if (!g_utf8_validate(changelog_txt, -1, NULL)) {
            gchar *converted = g_convert(changelog_txt, strlen(changelog_txt), "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
            if (converted != NULL) {
                g_free(changelog_txt);
                changelog_txt = converted;
            }
        }

        changelog_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(changelog_buffer, changelog_txt, -1);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);

        gtk_widget_show_all(scrolledwindow);
        gtk_widget_show_all(textview);
        gtk_widget_show_all(label);

        gtk_container_add(GTK_CONTAINER(changelog_notebook), scrolledwindow);
        gtk_container_set_border_width(GTK_CONTAINER(scrolledwindow), 2);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

        gtk_container_add(GTK_CONTAINER(scrolledwindow), textview);
        gtk_notebook_set_tab_label(GTK_NOTEBOOK(changelog_notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(changelog_notebook), changelogs), label);

        g_free(changelog_txt);
        g_free(source_url);
        changelogs++;
    }

    if (changelogs > 0) {
        gtk_widget_show_all(changelog_window);
    } else {
        gtk_widget_destroy(changelog_window);
        notify((gchar *)_("ChangeLogs"), _("No changelogs found."));
    }
}

void cancel_source_edit(GtkWidget *w, gpointer user_data)
{
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "source_window")));
}

void source_edit_ok(GtkWidget *w, gpointer user_data)
{
    SLAPT_PRIORITY_T priority;
    const char *original_url = NULL;
    const gchar *source = NULL;
    GtkEntry *source_entry = GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "source_entry"));
    GtkComboBox *source_priority = GTK_COMBO_BOX(gtk_builder_get_object(gslapt_builder, "source_priority"));
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeView *source_tree = GTK_TREE_VIEW(gtk_builder_get_object(gslapt_builder, "preferences_sources_treeview"));
    GtkTreeSelection *select = gtk_tree_view_get_selection(GTK_TREE_VIEW(source_tree));
    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(source_tree));

    source = gtk_entry_get_text(source_entry);

    if (source == NULL || strlen(source) < 1)
        return;

    priority = convert_gslapt_priority_to_slapt_priority(gtk_combo_box_get_active(source_priority));

    if ((original_url = g_object_get_data(G_OBJECT(gtk_builder_get_object(gslapt_builder, "source_window")), "original_url")) != NULL) {
        const char *priority_str = slapt_priority_to_str(priority);

        if (gtk_tree_selection_get_selected(select, &model, &iter))
            gtk_list_store_set(store, &iter, 1, source, 3, priority_str, 4, priority, -1);

    } else {
        const char *priority_str = slapt_priority_to_str(priority);
        GdkPixbuf *status_icon = gslapt_img("pkg_action_installed.png");
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, status_icon,
                           1, source,
                           2, TRUE,
                           3, priority_str,
                           4, priority,
                           -1);
        g_object_unref(status_icon);
    }

    sources_modified = TRUE;
    gtk_widget_destroy(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "source_window")));
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

GdkPixbuf *gslapt_img(const char *img)
{
    char buffer[2048];

    snprintf(buffer, 2047, "%s/%s/ui/%s", PACKAGE_DATA_DIR, PACKAGE, img);

    GdkPixbuf *i = gdk_pixbuf_new_from_file(buffer, NULL);
    return i;
}

void gslapt_load_ui(GtkBuilder *b, const char *f)
{
    GError *error = NULL;
    char buffer[2048];
    snprintf(buffer, 2047, "%s/%s/ui/%s", PACKAGE_DATA_DIR, PACKAGE, f);

    if (!gtk_builder_add_from_file(b, buffer, &error)) {
        g_warning("Couldn't load builder file: %s", error->message);
        g_error_free(error);
        exit(1);
    }
    gtk_builder_connect_signals(b, b);
}
