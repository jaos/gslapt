#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define _GNU_SOURCE

#include <gtk/gtk.h>

#include "callbacks.h"
#include "interface.h"
#include "support.h"


void on_gslapt_destroy (GtkObject *object, gpointer user_data) {
	(void)object;
	(void)user_data;
	gtk_main_quit ();
}

void update_callback (GtkObject *object, gpointer user_data) {
	GThread *gpd;

	(void)object;
	(void)user_data;

	gpd = g_thread_create((GThreadFunc)get_package_data,NULL,FALSE,NULL);

	return;
}

void upgrade_callback (GtkObject *object, gpointer user_data) {
	(void)object;
	(void)user_data;
	printf("upgrade callback\n");
}

void distupgrade_callback (GtkObject *object, gpointer user_data) {
	extern rc_config *global_config;
	(void)object;
	(void)user_data;
	printf("distupgrade callback\n");
	global_config->dist_upgrade = 1;
}

void execute_callback (GtkObject *object, gpointer user_data) {
	GtkWidget *trans_window;
	extern transaction_t *trans;
	(void)object;
	(void)user_data;
	(void)trans;

	trans_window = (GtkWidget *)create_transaction_window();

	gtk_widget_show(trans_window);
}

void quit_callback(GtkMenuItem *menuitem, gpointer user_data){
	(void)menuitem;
	(void)user_data;
	gtk_main_quit ();
}

void open_preferences (GtkMenuItem *menuitem, gpointer user_data) {
	GtkWidget *preferences;
	extern rc_config *global_config;

	(void)menuitem;
	(void)user_data;
	(void)global_config;

	preferences = (GtkWidget *)create_window_preferences();

	gtk_widget_show(preferences);
}

void on_search_tab_search_button_clicked (GtkButton *button, gpointer user_data) {
	extern GtkWidget *gslapt;
  GtkTreeView *treeview;
	gchar *pattern;

	(void)button;
	(void)user_data;
	
	/* search_tab_search_entry */
	pattern = (gchar *)gtk_entry_get_text(GTK_ENTRY(lookup_widget(gslapt,"search_tab_search_entry")));

	treeview = GTK_TREE_VIEW(lookup_widget(gslapt,"search_pkg_listing_treeview"));
	clear_treeview(treeview);
	build_searched_treeviewlist(GTK_WIDGET(treeview),pattern);
}

void add_pkg_for_install (GtkButton *button, gpointer user_data) {
	extern GtkWidget *gslapt;
	extern transaction_t *trans;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;
	extern rc_config *global_config;
	pkg_info_t *pkg;
	pkg_info_t *installed_pkg;
	GtkEntry *entry;
	const gchar *pkg_name;
	const gchar *pkg_version;

	(void)button;
	(void)user_data;

	/* lookup pkg from form */
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_name_entry") );
	pkg_name = gtk_entry_get_text(GTK_ENTRY(entry));
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_version_entry") );
	pkg_version = gtk_entry_get_text(GTK_ENTRY(entry));

	if( pkg_name == NULL ) return;

	pkg = get_newest_pkg(all,pkg_name);
	installed_pkg = get_newest_pkg(installed,pkg_name);
	/* find out if this is a new install or an upgrade */
	if( installed_pkg == NULL ){
		guint c; gint dep_return = -1;
		struct pkg_list *deps = init_pkg_list();

		/* int get_pkg_dependencies(const rc_config *global_config,struct pkg_list *avail_pkgs,struct pkg_list *installed_pkgs,pkg_info_t *pkg,struct pkg_list *deps); */
		dep_return = get_pkg_dependencies(global_config,all,installed,pkg,deps);

		if( dep_return == -1 ){
			add_exclude_to_transaction(trans,pkg);
		}else{
			for(c = 0; c < deps->pkg_count;c++){
				/* only check if it's not already present in trans */
				if( search_transaction(trans,deps->pkgs[c]->name) == 0 ){
					pkg_info_t *dep_installed;

					if( (dep_installed = get_newest_pkg(installed,deps->pkgs[c]->name)) == NULL ){
						add_install_to_transaction(trans,deps->pkgs[c]);
					}else{
						/* add only if its a valid upgrade */
						if(cmp_pkg_versions(dep_installed->version,deps->pkgs[c]->version) < 0 )
							add_upgrade_to_transaction(trans,dep_installed,deps->pkgs[c]);
					}
				}
			}
			if( search_transaction(trans,pkg->name) == 0 )
				add_install_to_transaction(trans,pkg);

			free(deps->pkgs);
			free(deps);
		}

	}else{
		if( cmp_pkg_versions(installed_pkg->version,pkg->version) < 0){
			guint c; gint dep_return = -1;
			struct pkg_list *deps = init_pkg_list();

			dep_return = get_pkg_dependencies(global_config,all,installed,pkg,deps);
			if( dep_return == -1 ){
				add_exclude_to_transaction(trans,pkg);
			}else{
				for(c = 0; c < deps->pkg_count;c++){
					/* only check if it's not already present in trans */
					if( search_transaction(trans,deps->pkgs[c]->name) == 0 ){
						pkg_info_t *dep_installed;
						if( (dep_installed = get_newest_pkg(installed,deps->pkgs[c]->name)) == NULL ){
							add_install_to_transaction(trans,deps->pkgs[c]);
						}else{
							/* add only if its a valid upgrade */
							if(cmp_pkg_versions(dep_installed->version,deps->pkgs[c]->version) < 0 )
								add_upgrade_to_transaction(trans,installed_pkg,deps->pkgs[c]);
						}
					}
				}/* end for loop */
				if( search_transaction(trans,pkg->name) == 0 )
					add_upgrade_to_transaction(trans,installed_pkg,pkg);
			}
			free(deps->pkgs);
			free(deps);
		}
	}

}

void add_pkg_for_removal (GtkButton *button, gpointer user_data) {
	extern GtkWidget *gslapt;
	extern transaction_t *trans;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;
	extern rc_config *global_config;
	pkg_info_t *pkg;
	GtkEntry *entry;
	const gchar *pkg_name;
	const gchar *pkg_version;

	(void)button;
	(void)user_data;

	/* lookup pkg from form */
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_name_entry") );
	pkg_name = gtk_entry_get_text(GTK_ENTRY(entry));
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_version_entry") );
	pkg_version = gtk_entry_get_text(GTK_ENTRY(entry));

	if( (pkg = get_newest_pkg(installed,pkg_name)) != NULL ){
		guint c;
		struct pkg_list *deps;

		deps = is_required_by(global_config,all,pkg);

		for(c = 0; c < deps->pkg_count;c++){
			/* if not already in the transaction, add if installed */
			if( search_transaction(trans,deps->pkgs[c]->name) == 0 ){
				if( get_newest_pkg(installed,deps->pkgs[c]->name) != NULL ){
					add_remove_to_transaction(trans,deps->pkgs[c]);
				}
			}
		}

		free(deps->pkgs);
		free(deps);

		if( search_transaction(trans,pkg->name) == 0 )
			add_remove_to_transaction(trans,pkg);

	}

}

void add_pkg_for_exclude (GtkButton *button, gpointer user_data) {
	extern GtkWidget *gslapt;
	extern transaction_t *trans;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;
	GtkEntry *entry;
	const gchar *pkg_name;
	const gchar *pkg_version;
	guint i;

	(void)button;
	(void)user_data;

	/* lookup pkg from form */
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_name_entry") );
	pkg_name = gtk_entry_get_text(entry);
	entry = GTK_ENTRY( lookup_widget(gslapt,"pkg_info_action_version_entry") );
	pkg_version = gtk_entry_get_text(GTK_ENTRY(entry));

	/* exclude pkgs from available and installed */
	for(i = 0; i < installed->pkg_count;i++){
		if( strcmp(installed->pkgs[i]->name,pkg_name) == 0
			&& strcmp(installed->pkgs[i]->version,pkg_version) == 0
		){
			if( search_transaction(trans,installed->pkgs[i]->name) == 0)
				add_exclude_to_transaction(trans,installed->pkgs[i]);
		}
	}
	for(i = 0; i < all->pkg_count;i++){
		if( strcmp(all->pkgs[i]->name,pkg_name) == 0
			&& strcmp(all->pkgs[i]->version,pkg_version) == 0
		){
			if( search_transaction(trans,all->pkgs[i]->name) == 0)
				add_exclude_to_transaction(trans,all->pkgs[i]);
		}
	}

	return;
}

void build_installed_treeviewlist(GtkWidget *treeview){
	GtkListStore *store;
	GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
	GtkTreeSelection *select;
	extern struct pkg_list *installed;
	guint i = 0;

	store = gtk_list_store_new (
		3, /* name, version */
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
	);

	for(i = 0; i < installed->pkg_count; i++ ){
		gtk_list_store_append (store, &iter);
		gtk_list_store_set ( store, &iter,
			0,installed->pkgs[i]->name, 1,installed->pkgs[i]->version, 2,installed->pkgs[i]->location,-1
		);
	}

  /* column for name */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
		"text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for version */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Version", renderer,
		"text", 1, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 1);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

	gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (select), "changed",
		G_CALLBACK (show_pkg_details), NULL);

}


void build_available_treeviewlist(GtkWidget *treeview){
	GtkListStore *store;
	GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
	GtkTreeSelection *select;
	guint i = 0;
	extern struct pkg_list *all;
	extern struct pkg_list *installed;

	store = gtk_list_store_new (
		3, /* name, version, location */
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
	);

	for(i = 0; i < all->pkg_count; i++ ){
		if( get_pkg_by_details(
					installed,
					all->pkgs[i]->name,
					all->pkgs[i]->version,
					all->pkgs[i]->location
				) == NULL
		){
			gtk_list_store_append (store, &iter);
			gtk_list_store_set ( store, &iter,
				0,all->pkgs[i]->name, 1,all->pkgs[i]->version, 2,all->pkgs[i]->location, -1
			);
		}
	}

  /* column for name */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
		"text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for version */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Version", renderer,
		"text", 1, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 1);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for location */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Location", renderer,
		"text", 2, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 2);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

	gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (select), "changed",
		G_CALLBACK (show_pkg_details), all);

}


void build_searched_treeviewlist(GtkWidget *treeview, gchar *pattern){
	GtkListStore *store;
	GtkTreeIter iter;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
	GtkTreeSelection *select;
	guint i = 0;
	extern struct pkg_list *all;
	extern struct pkg_list *installed;

	store = gtk_list_store_new (
		4, /* name, version, location, installed */
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING
	);

	if( pattern != NULL ){
		struct pkg_list *a_matches,*i_matches;

		a_matches = search_pkg_list(all,pattern);
		for(i = 0; i < a_matches->pkg_count; i++ ){
			gchar installed_notification[4];
			if(
				get_pkg_by_details(
					installed,
					a_matches->pkgs[i]->name,
					a_matches->pkgs[i]->version,
					NULL
				) != NULL
			){
				strcpy(installed_notification,"Yes\0");
			}else{
				strcpy(installed_notification,"No\0");
			}
			gtk_list_store_append (store, &iter);
			gtk_list_store_set ( store, &iter,
				0,a_matches->pkgs[i]->name,
				1,a_matches->pkgs[i]->version,
				2,a_matches->pkgs[i]->location,
				3,installed_notification,-1
			);
		}
		free(a_matches->pkgs);
		free(a_matches);

		i_matches = search_pkg_list(installed,pattern);
		for(i = 0; i < i_matches->pkg_count; i++ ){
			gchar installed_notification[4];
			if(
				get_pkg_by_details(
					installed,
					i_matches->pkgs[i]->name,
					i_matches->pkgs[i]->version,
					NULL
				) != NULL
			){
				strcpy(installed_notification,"Yes\0");
			}else{
				strcpy(installed_notification,"No\0");
			}
			gtk_list_store_append (store, &iter);
			gtk_list_store_set ( store, &iter,
				0,i_matches->pkgs[i]->name,
				1,i_matches->pkgs[i]->version,
				2,i_matches->pkgs[i]->location,
				3,installed_notification,-1
			);
		}
		free(i_matches->pkgs);
		free(i_matches);

	}

  /* column for name */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
		"text", 0, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for version */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Version", renderer,
		"text", 1, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 1);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for location */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Location", renderer,
		"text", 2, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 2);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);

  /* column for install */
  renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Installed?", renderer,
		"text", 3, NULL);
  gtk_tree_view_column_set_sort_column_id (column, 3);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);


	gtk_tree_view_set_model (GTK_TREE_VIEW(treeview),GTK_TREE_MODEL(store));

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
	gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
	g_signal_connect (G_OBJECT (select), "changed",
		G_CALLBACK (show_pkg_details), all);

}


void open_about (GtkObject *object, gpointer user_data) {
	GtkWidget *about;
	(void)object;
	(void)user_data;
	about = (GtkWidget *)create_about();
	gtk_widget_show (about);
}

void show_pkg_details (GtkTreeSelection *selection, gpointer data) {
	GtkTreeIter iter;
	GtkTreeModel *model;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;

	(void)data;

	if (gtk_tree_selection_get_selected (selection, &model, &iter)){
		gchar *p_name,*p_version,*p_location;
		pkg_info_t *pkg;

		gtk_tree_model_get (model, &iter, 0, &p_name, -1);
		gtk_tree_model_get (model, &iter, 1, &p_version, -1);
		gtk_tree_model_get (model, &iter, 2, &p_location, -1);

		pkg = get_pkg_by_details(all,p_name,p_version,p_location);
		if( pkg != NULL ){
			fillin_pkg_details(pkg);
		}else{
			pkg = get_pkg_by_details(installed,p_name,p_version,p_location);
			if( pkg != NULL )
				fillin_pkg_details(pkg);
		}

		g_free (p_name);
		g_free (p_version);
		g_free (p_location);
	}

}

void fillin_pkg_details(pkg_info_t *pkg){
  extern GtkWidget *gslapt;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;
	gchar size_c[20],size_u[20],*short_desc;
	GtkButton *install_upgrade;
	GtkButton *remove;
	GtkButton *exclude;
	pkg_info_t *installed_pkg;
	pkg_info_t *upgrade_pkg;

	/* lookup buttons */
	install_upgrade = GTK_BUTTON( lookup_widget(gslapt,"pkg_info_action_install_upgrade_button") );
	remove = GTK_BUTTON( lookup_widget(gslapt,"pkg_info_action_remove_button") );
	exclude = GTK_BUTTON( lookup_widget(gslapt,"pkg_info_action_exclude_button") );

	/* set default state */
	gtk_widget_set_sensitive( GTK_WIDGET(install_upgrade),TRUE);
	gtk_widget_set_sensitive( GTK_WIDGET(remove),TRUE);
	gtk_widget_set_sensitive( GTK_WIDGET(exclude),TRUE);

	gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_name_entry")),pkg->name);
	gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_version_entry")),pkg->version);

	installed_pkg = get_newest_pkg(installed,pkg->name);
	upgrade_pkg = get_newest_pkg(all,pkg->name);

	if( strcmp(pkg->location,"") != 0 && strcmp(pkg->description,"") != 0 ){
		gtk_widget_set_sensitive( GTK_WIDGET(remove),FALSE);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_location_entry")),pkg->location);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_mirror_entry")),pkg->mirror);
		sprintf(size_c,"%d K",pkg->size_c);
		sprintf(size_u,"%d K",pkg->size_u);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_size_entry")),size_c);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_isize_entry")),size_u);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_required_entry")),pkg->required);
		short_desc = gen_short_pkg_description(pkg);
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_description_entry")),short_desc);
		free(short_desc);
	}else{
		/* if there is no possible upgrade available */
		if(
			upgrade_pkg != NULL
			&& installed_pkg != NULL
			&& cmp_pkg_versions(installed_pkg->version,upgrade_pkg->version) >= 0
		){
			gtk_widget_set_sensitive( GTK_WIDGET(install_upgrade),FALSE);
		}
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_location_entry")),"");
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_mirror_entry")),"");
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_size_entry")),"");
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_isize_entry")),"");
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_required_entry")),"");
		gtk_entry_set_text(GTK_ENTRY(lookup_widget(gslapt,"pkg_info_action_description_entry")),"");
	}

}

void clear_treeview(GtkTreeView *treeview){
	GtkListStore *store;
	GList *columns;
	guint i;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));
	gtk_list_store_clear(store);
	columns = gtk_tree_view_get_columns(treeview);
	for(i = 0; i < g_list_length(columns); i++ ){
		GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(g_list_nth_data(columns,i));
		if( column != NULL )
			gtk_tree_view_remove_column(treeview,column);
	}
	g_list_free(columns);
}

int lget_mirror_data_from_source(FILE *fh,const char *base_url,const char *filename){
	gint return_code = 0;
	gchar *url = NULL;
	guint context_id;

	url = calloc(
		strlen(base_url) + strlen(filename) + 1, sizeof *url
	);
	if( url == NULL ){
		/* fprintf(stderr,_("Failed to calloc url\n")); */
		exit(1);
	}

	strncpy(url,base_url,strlen(base_url) );
	url[ strlen(base_url) ] = '\0';
	strncat(url,filename,strlen(filename) );
	fprintf(stderr,"fetching %s\n",url);
	context_id = gslapt_set_status(url);
	return_code = ldownload_data(fh,url);
	gslapt_clear_status(context_id);

	free(url);
	/* make sure we are back at the front of the file */
	/* DISABLED */
	/* rewind(fh); */
	return return_code;
}

int ldownload_data(FILE *fh,const char *url ){
	CURL *ch = NULL;
	CURLcode response;
	gchar curl_err_buff[1024];
	gint return_code = 0;

	ch = curl_easy_init();
	curl_easy_setopt(ch, CURLOPT_URL, url);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, fh);
	curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(ch, CURLOPT_USERAGENT, "gslapt" );
	curl_easy_setopt(ch, CURLOPT_USERPWD, "anonymous:slapt-get-user@software.jaos.org");
	curl_easy_setopt(ch, CURLOPT_ERRORBUFFER, curl_err_buff );
	curl_easy_setopt(ch, CURLOPT_PROGRESSFUNCTION, gtk_progress_callback );

	if( (response = curl_easy_perform(ch)) != 0 ){
		/* fprintf(stderr,_("Failed to download: %s\n"),curl_err_buff); */
		return_code = -1;
	}
	/*
   * need to use curl_easy_cleanup() so that we don't 
 	 * have tons of open connections, getting rejected
	 * by ftp servers for being naughty.
	*/
	curl_easy_cleanup(ch);
	/* can't do a curl_free() after curl_easy_cleanup() */
	/* curl_free(ch); */

	return return_code;
}

void get_package_data(void){
	extern rc_config *global_config;
	guint i;
	gint source_dl_failed = 0;
	FILE *pkg_list_fh_tmp = NULL;
	extern GtkWidget *gslapt;

	gdk_threads_enter();
	lock_toolbar_buttons();
	gdk_threads_leave();

	/* open tmp pkg list file */
	pkg_list_fh_tmp = tmpfile();
	if( pkg_list_fh_tmp == NULL ){
		if( errno ) perror("tmpfile");
		exit(1);
	}

	/* go through each package source and download the meta data */
	for(i = 0; i < global_config->sources.count; i++){
		FILE *tmp_pkg_f,*tmp_patch_f,*tmp_checksum_f;
		struct pkg_list *available_pkgs = NULL;
		struct pkg_list *patch_pkgs = NULL;
		gchar *pkg_filename,*patch_filename,*checksum_filename;
		gchar *pkg_head,*pkg_local_head;
		gchar *patch_head,*patch_local_head;
		gchar *checksum_head,*checksum_local_head;

		/* download our PKG_LIST */
		pkg_filename = gen_filename_from_url(global_config->sources.url[i],PKG_LIST);
		pkg_head = head_mirror_data(global_config->sources.url[i],PKG_LIST);
		pkg_local_head = read_head_cache(pkg_filename);

		/* open for reading if cached, otherwise write it from the downloaded data */
		if( pkg_head != NULL && pkg_local_head != NULL && strcmp(pkg_head,pkg_local_head) == 0){
			if( (tmp_pkg_f = open_file(pkg_filename,"r")) == NULL ) exit(1);
			available_pkgs = parse_packages_txt(tmp_pkg_f);
		}else{
			if( (tmp_pkg_f = open_file(pkg_filename,"w+b")) == NULL ) exit(1);
			if( lget_mirror_data_from_source(tmp_pkg_f,global_config->sources.url[i],PKG_LIST) == 0 ){
				rewind(tmp_pkg_f); /* make sure we are back at the front of the file */
				available_pkgs = parse_packages_txt(tmp_pkg_f);
			}else{
				source_dl_failed = 1;
				clear_head_cache(pkg_filename);
			}
		}
		if( available_pkgs == NULL || available_pkgs->pkg_count < 1 ){
			source_dl_failed = 1;
			clear_head_cache(pkg_filename);
		}
		/* if all is good, write it */
		if( source_dl_failed != 1 && pkg_head != NULL ) write_head_cache(pkg_head,pkg_filename);
		free(pkg_head);
		free(pkg_local_head);
		free(pkg_filename);
		fclose(tmp_pkg_f);


		/* download PATCHES_LIST */
		patch_filename = gen_filename_from_url(global_config->sources.url[i],PATCHES_LIST);
		patch_head = head_mirror_data(global_config->sources.url[i],PATCHES_LIST);
		patch_local_head = read_head_cache(patch_filename);

		/* open for reading if cached, otherwise write it from the downloaded data */
		if( patch_head != NULL && patch_local_head != NULL && strcmp(patch_head,patch_local_head) == 0){
			if( (tmp_patch_f = open_file(patch_filename,"r")) == NULL ) exit(1);
			patch_pkgs = parse_packages_txt(tmp_patch_f);
		}else{
			if( (tmp_patch_f = open_file(patch_filename,"w+b")) == NULL ) exit (1);
			if( lget_mirror_data_from_source(tmp_patch_f,global_config->sources.url[i],PATCHES_LIST) == 0 ){
				rewind(tmp_patch_f); /* make sure we are back at the front of the file */
				patch_pkgs = parse_packages_txt(tmp_patch_f);
			}else{
				/* we don't care if the patch fails, for example current doesn't have patches */
				/* source_dl_failed = 1; */
				clear_head_cache(patch_filename);
			}
		}
		/* if all is good, write it */
		if( source_dl_failed != 1 && patch_head != NULL ) write_head_cache(patch_head,patch_filename);
		free(patch_head);
		free(patch_local_head);
		free(patch_filename);
		fclose(tmp_patch_f);


		/* download checksum file */
		checksum_filename = gen_filename_from_url(global_config->sources.url[i],CHECKSUM_FILE);
		checksum_head = head_mirror_data(global_config->sources.url[i],CHECKSUM_FILE);
		checksum_local_head = read_head_cache(checksum_filename);

		/* open for reading if cached, otherwise write it from the downloaded data */
		if( checksum_head != NULL && checksum_local_head != NULL && strcmp(checksum_head,checksum_local_head) == 0){
			if( (tmp_checksum_f = open_file(checksum_filename,"r")) == NULL ) exit(1);
		}else{
			if( (tmp_checksum_f = open_file(checksum_filename,"w+b")) == NULL ) exit(1);
			if( lget_mirror_data_from_source(
						tmp_checksum_f,global_config->sources.url[i],CHECKSUM_FILE
					) != 0
			){
				source_dl_failed = 1;
				clear_head_cache(checksum_filename);
			}else{
			}
			rewind(tmp_checksum_f); /* make sure we are back at the front of the file */
		}
		/* if all is good, write it */
		if( source_dl_failed != 1 && checksum_head != NULL ) write_head_cache(checksum_head,checksum_filename);
		free(checksum_head);
		free(checksum_local_head);

		/*
			only do this double check if we know it didn't fail
		*/
		if( source_dl_failed != 1 ){
			if( available_pkgs->pkg_count == 0 ) source_dl_failed = 1;
		}

		/* if the download failed don't do this, do it if cached or d/l was good */
		if( source_dl_failed != 1 ){
			guint a;

			/* now map md5 checksums to packages */
			for(a = 0;a < available_pkgs->pkg_count;a++){
				get_md5sum(available_pkgs->pkgs[a],tmp_checksum_f);
			}
			for(a = 0;a < patch_pkgs->pkg_count;a++){
				get_md5sum(patch_pkgs->pkgs[a],tmp_checksum_f);
			}

			/* write package listings to disk */
			write_pkg_data(global_config->sources.url[i],pkg_list_fh_tmp,available_pkgs);
			write_pkg_data(global_config->sources.url[i],pkg_list_fh_tmp,patch_pkgs);

		}
		if ( available_pkgs ) free_pkg_list(available_pkgs);
		if ( patch_pkgs ) free_pkg_list(patch_pkgs);
		free(checksum_filename);
		fclose(tmp_checksum_f);

	}/* end for loop */

	/* if all our downloads where a success, write to PKG_LIST_L */
	if( source_dl_failed != 1 ){
		ssize_t bytes_read;
		size_t getline_len = 0;
		gchar *getline_buffer = NULL;
		FILE *pkg_list_fh;

		if( (pkg_list_fh = open_file(PKG_LIST_L,"w+")) == NULL ) exit(1);
		if( pkg_list_fh == NULL ) exit(1);
		rewind(pkg_list_fh_tmp);
		while( (bytes_read = getline(&getline_buffer,&getline_len,pkg_list_fh_tmp) ) != EOF ){
			fprintf(pkg_list_fh,"%s",getline_buffer);
		}
		if( getline_buffer ) free(getline_buffer);
		fclose(pkg_list_fh);

	}

	/* close the tmp pkg list file */
	fclose(pkg_list_fh_tmp);

	gdk_threads_enter();
	unlock_toolbar_buttons();
	rebuild_treeviews();
	gdk_threads_leave();

}

int gtk_progress_callback(void *data, double dltotal, double dlnow, double ultotal, double ulnow){
	extern GtkWidget *gslapt;

	(void)gslapt;
	(void)data;
	(void)ultotal;
	(void)ulnow;

	gdk_threads_enter();
	gdk_threads_leave();

	return 0;
}

void rebuild_treeviews(void){
	extern GtkWidget *gslapt;
  GtkWidget *treeview;
	extern struct pkg_list *installed;
	extern struct pkg_list *all;
	struct pkg_list *all_ptr,*installed_ptr;

	treeview = (GtkWidget *)lookup_widget(gslapt,"inst_pkg_listing_treeview");
	clear_treeview( GTK_TREE_VIEW(treeview) );
	installed_ptr = installed;
	installed = get_installed_pkgs();
	build_installed_treeviewlist(treeview);
	free_pkg_list(installed_ptr);

	treeview = (GtkWidget *)lookup_widget(gslapt,"available_pkg_listing_treeview");
	clear_treeview( GTK_TREE_VIEW(treeview) );
	all_ptr = all;
	all = get_available_pkgs();
	build_available_treeviewlist(treeview);
	free_pkg_list(all_ptr);

}

guint gslapt_set_status(const gchar *msg){
	extern GtkWidget *gslapt;
	guint context_id;
	GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));
	context_id = gtk_statusbar_get_context_id(bar,msg);

	fprintf(stderr,"setting status\n");
	gtk_statusbar_push(bar,context_id,msg);

	return context_id;
}

void gslapt_clear_status(guint context_id){
	extern GtkWidget *gslapt;
	GtkStatusbar *bar = GTK_STATUSBAR(lookup_widget(gslapt,"bottom_statusbar"));

	gtk_statusbar_pop(bar,context_id);
	fprintf(stderr,"cleared status\n");
}

void on_preferences_buttons_cancel_clicked (GtkButton *button, gpointer user_data) {
	GtkWidget *preferences = (GtkWidget *)user_data;
	(void)button;
	gtk_widget_destroy(preferences);
}

void on_preferences_buttons_ok_clicked (GtkButton *button, gpointer user_data) {
	GtkWidget *preferences = (GtkWidget *)user_data;
	(void)button;
	gtk_widget_destroy(preferences);
}

void on_preferences_buttons_apply_clicked (GtkButton *button, gpointer user_data) { 
	GtkWidget *preferences = (GtkWidget *)user_data;
	(void)button;
	gtk_widget_destroy(preferences);
}

void on_transaction_report_cancel_button_clicked (GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
}

void on_transaction_report_ok_button_clicked (GtkButton *button, gpointer user_data) {
	(void)button;
	(void)user_data;
}

void lock_toolbar_buttons(void){
  extern GtkWidget *gslapt;
  GtkToolButton *action_bar_update_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_update_button") );
  GtkToolButton *action_bar_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_upgrade_button") );
  GtkToolButton *action_bar_dist_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_dist_upgrade_button") );
  GtkToolButton *action_bar_execute_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_execute_button") );

	gtk_widget_set_sensitive((GtkWidget *)action_bar_update_button,FALSE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_upgrade_button,FALSE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_dist_upgrade_button,FALSE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_execute_button,FALSE);
}

void unlock_toolbar_buttons(void){
  extern GtkWidget *gslapt;
  GtkToolButton *action_bar_update_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_update_button") );
  GtkToolButton *action_bar_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_upgrade_button") );
  GtkToolButton *action_bar_dist_upgrade_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_dist_upgrade_button") );
  GtkToolButton *action_bar_execute_button = GTK_TOOL_BUTTON( lookup_widget(gslapt,"action_bar_execute_button") );

	gtk_widget_set_sensitive((GtkWidget *)action_bar_update_button,TRUE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_upgrade_button,TRUE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_dist_upgrade_button,TRUE);
	gtk_widget_set_sensitive((GtkWidget *)action_bar_execute_button,TRUE);
}

void preferences_sources_add(GtkButton *button, gpointer user_data){
	(void)button;
	(void)user_data;
}

void preferences_sources_remove(GtkButton *button, gpointer user_data){
	(void)button;
	(void)user_data;
}

void preferences_on_apply_clicked(GtkWidget *w, gpointer user_data){
	(void)user_data;
	gtk_widget_destroy(w);
}

void preferences_on_ok_clicked(GtkWidget *w, gpointer user_data){
	(void)user_data;
	gtk_widget_destroy(w);
}


void on_transaction_okbutton1_clicked(GtkWidget *w, gpointer user_data){
	(void)user_data;
	gtk_widget_destroy(w);
}


