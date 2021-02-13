/*
 * Copyright (C) 2003-2021 Jason Woodward <woodwardj at jaos dot org>
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

#include <config.h>

#include <gtk/gtk.h>
#include "callbacks.h"
#include "settings.h"
#include "series.h"

slapt_config_t *global_config; /* our config struct */
slapt_vector_t *installed;
slapt_vector_t *all;
GtkWidget *gslapt;
GtkBuilder *gslapt_builder;
slapt_transaction_t *trans = NULL;
char rc_location[1024];
GslaptSettings *gslapt_settings = NULL;
GHashTable *gslapt_series_map = NULL;

int main(int argc, char *argv[])
{
    GtkStatusbar *bar;
    guint default_context_id;
    GtkEntryCompletion *completions;
    slapt_vector_t *pkg_names_to_install = slapt_vector_t_init(free);
    slapt_vector_t *pkg_names_to_remove = slapt_vector_t_init(free);
    gchar *rc = NULL;
    bool do_upgrade = false;

    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    textdomain(GETTEXT_PACKAGE);

#ifdef SLAPT_HAS_GPGME
    gpgme_check_version(NULL);
#endif

    gtk_init(&argc, &argv);

    trans = slapt_init_transaction();

    /* series name mapping */
    gslapt_series_map = gslapt_series_map_init();
    gslapt_series_map_fill(gslapt_series_map);

    for (int option_index = 1; option_index < argc; ++option_index) {
        if (strcmp(argv[option_index], "--upgrade") == 0) {
            do_upgrade = true;

        } else if (strcmp(argv[option_index], "--config") == 0) {
            if (argc > (option_index + 1) &&
                strcmp(argv[option_index + 1], "--upgrade") != 0 &&
                strcmp(argv[option_index + 1], "--config") != 0 &&
                strcmp(argv[option_index + 1], "--remove") != 0)
                rc = argv[++option_index];

        } else if (strcmp(argv[option_index], "--install") == 0) {
            char *next_opt = NULL;

            if (argc > (option_index + 1) &&
                strcmp(argv[option_index + 1], "--upgrade") != 0 &&
                strcmp(argv[option_index + 1], "--config") != 0 &&
                strcmp(argv[option_index + 1], "--remove") != 0)
                next_opt = argv[++option_index];

            while (next_opt != NULL) {
                slapt_vector_t_add(pkg_names_to_install, strdup(next_opt));

                if (argc > (option_index + 1) &&
                    strcmp(argv[option_index + 1], "--upgrade") != 0 &&
                    strcmp(argv[option_index + 1], "--config") != 0 &&
                    strcmp(argv[option_index + 1], "--remove") != 0)
                    next_opt = argv[++option_index];
                else
                    next_opt = NULL;
            }

        } else if (strcmp(argv[option_index], "--remove") == 0) {
            char *next_opt = NULL;

            if (argc > (option_index + 1) &&
                strcmp(argv[option_index + 1], "--upgrade") != 0 &&
                strcmp(argv[option_index + 1], "--config") != 0 &&
                strcmp(argv[option_index + 1], "--install") != 0)
                next_opt = argv[++option_index];

            while (next_opt != NULL) {
                slapt_vector_t_add(pkg_names_to_remove, strdup(next_opt));

                if (argc > (option_index + 1) &&
                    strcmp(argv[option_index + 1], "--upgrade") != 0 &&
                    strcmp(argv[option_index + 1], "--config") != 0 &&
                    strcmp(argv[option_index + 1], "--install") != 0)
                    next_opt = argv[++option_index];
                else
                    next_opt = NULL;
            }
        }
    }

    if (rc == NULL) {
        global_config = slapt_config_t_read(RC_LOCATION);
        strncpy(rc_location, RC_LOCATION, 1023);
    } else {
        global_config = slapt_config_t_read(rc);
        strncpy(rc_location, rc, 1023);
    }
    if (global_config == NULL)
        exit(1);
    slapt_working_dir_init(global_config);
    if (chdir(global_config->working_dir) == -1) {
        exit(1);
    }
    global_config->progress_cb = gtk_progress_callback;

    /* read in all pkgs and installed pkgs */
    installed = slapt_get_installed_pkgs();
    all = slapt_get_available_pkgs();

    gslapt_builder = gtk_builder_new();
    gtk_builder_set_translation_domain(gslapt_builder, GETTEXT_PACKAGE);
    gslapt_load_ui(gslapt_builder, "gslapt.ui");

    gslapt = GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "gslapt"));
    gtk_builder_connect_signals(gslapt_builder, NULL);
    // g_object_unref (G_OBJECT (gslapt_builder));

    completions = build_search_completions();
    gtk_entry_set_completion(GTK_ENTRY(gtk_builder_get_object(gslapt_builder, "search_entry")), completions);
    g_object_unref(completions);

    /* weird issue with 14.2 Gtk+ 3.18:
     * Gtk-CRITICAL **: gtk_widget_get_preferred_width_for_height: assertion 'height >= 0' failed
     * unless we set the min-content-height
     */
#if GTK_CHECK_VERSION(3,0,0)
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(gtk_builder_get_object(gslapt_builder, "pkg_list_scrolled")), 40);
#endif
    build_treeview_columns(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview")));
    build_package_treeviewlist(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "pkg_listing_treeview")));

    bar = GTK_STATUSBAR(gtk_builder_get_object(gslapt_builder, "bottom_statusbar"));
    default_context_id = gtk_statusbar_get_context_id(bar, "default");
    gtk_statusbar_push(bar, default_context_id, (gchar *)_("Ready"));

    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "action_bar_execute_button")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "execute1")), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(gslapt_builder, "unmark_all1")), FALSE);

    /* restore previous rc settings */
    gslapt_settings = gslapt_read_rc();
    if (gslapt_settings == NULL) {
        gslapt_settings = gslapt_new_rc();
        gtk_window_set_default_size(GTK_WINDOW(gslapt), 640, 480);
    } else {
        gtk_window_set_default_size(GTK_WINDOW(gslapt), gslapt_settings->width, gslapt_settings->height);
        gtk_window_move(GTK_WINDOW(gslapt), gslapt_settings->x, gslapt_settings->y);
    }

    gtk_widget_show_all(gslapt);

    if (do_upgrade) {
        g_signal_emit_by_name(gtk_builder_get_object(gslapt_builder, "action_bar_upgrade_button"), "clicked");
    } else {
        if (pkg_names_to_install->size > 0) {
            slapt_vector_t_foreach (char *, pkg_name_to_install, pkg_names_to_install) {
                slapt_pkg_t *p = slapt_get_newest_pkg(all, pkg_name_to_install);
                slapt_pkg_t *inst_p = slapt_get_newest_pkg(installed, pkg_name_to_install);

                if (p == NULL)
                    continue;

                if (inst_p != NULL && slapt_cmp_pkgs(inst_p, p) == 0) {
                    continue;
                } else if (inst_p != NULL && slapt_cmp_pkgs(inst_p, p) < 0) {
                    if (slapt_add_deps_to_trans(global_config, trans, all, installed, p) == 0) {
                        slapt_add_upgrade_to_transaction(trans, inst_p, p);
                    } else {
                        exit(1);
                    }
                } else {
                    if (slapt_add_deps_to_trans(global_config, trans, all, installed, p) == 0) {
                        slapt_vector_t *conflicts = slapt_is_conflicted(trans, all, installed, p);
                        slapt_add_install_to_transaction(trans, p);
                        if (conflicts->size > 0) {
                            slapt_vector_t_foreach (slapt_pkg_t *, conflict_pkg, conflicts) {
                                slapt_add_remove_to_transaction(trans, conflict_pkg);
                            }
                        }
                        slapt_vector_t_free(conflicts);
                    } else {
                        exit(1);
                    }
                }
            }
        }
        if (pkg_names_to_remove->size > 0) {
            slapt_vector_t_foreach (char *, pkg_name_to_remove, pkg_names_to_remove) {
                slapt_pkg_t *r = slapt_get_newest_pkg(installed, pkg_name_to_remove);
                if (r != NULL) {
                    slapt_add_remove_to_transaction(trans, r);
                }
            }
        }
    }

    slapt_vector_t_free(pkg_names_to_install);
    slapt_vector_t_free(pkg_names_to_remove);

    if (trans->remove_pkgs->size > 0 || trans->install_pkgs->size > 0 || trans->upgrade_pkgs->size > 0) {
        g_signal_emit_by_name(gtk_builder_get_object(gslapt_builder, "action_bar_execute_button"), "clicked");
    }

    gtk_main();

    return 0;
}
