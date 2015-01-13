/*
 * Copyright (C) 2003-2015 Jason Woodward <woodwardj at jaos dot org>
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

#include "settings.h"

char *gslapt_init_rc_dir (void)
{
  char *dir = g_build_path (G_DIR_SEPARATOR_S, g_get_user_config_dir (), PACKAGE, NULL);

  if (g_file_test(dir, G_FILE_TEST_IS_DIR) == FALSE) {
    if ( g_mkdir_with_parents(dir, 0755) !=0 ) {
      g_free(dir);
      return NULL;
    }
  }
  return dir;
}

GslaptSettings *gslapt_new_rc (void)
{
  GslaptSettings *g = g_slice_new(GslaptSettings);
  g->x      = g->y      = g->width      = g->height      = 0;
  g->cl_x   = g->cl_y   = g->cl_width   = g->cl_height   = 0;
  g->pref_x = g->pref_y = g->pref_width = g->pref_height = 0;
  g->tran_x = g->tran_y = g->tran_width = g->tran_height = 0;

  return g;
}

GslaptSettings *gslapt_read_rc (void)
{
  GslaptSettings *gslapt_settings = NULL;
  char *dir = NULL, *file = NULL;
  
  dir = gslapt_init_rc_dir();

  if (dir == NULL)
    return NULL;

  file = g_build_path (G_DIR_SEPARATOR_S, dir, "rc", NULL);
  free(dir);

  if (file == NULL)
    return NULL;


  if (g_file_test(file, G_FILE_TEST_IS_REGULAR) == TRUE) {
    GKeyFile *keyfile= NULL;
    GKeyFileFlags flags = G_KEY_FILE_NONE;
    GError *error = NULL;

    keyfile = g_key_file_new();
    if (keyfile == NULL)
      goto GSLAPT_READ_CONFIG_END;

    if (!g_key_file_load_from_file (keyfile, file, flags, &error)) {
      if (error != NULL)
        g_error_free(error);
      if (keyfile != NULL)
        g_key_file_free(keyfile);
      goto GSLAPT_READ_CONFIG_END;
    }

    gslapt_settings = g_slice_new(GslaptSettings);
    if (gslapt_settings == NULL) {
      if (error != NULL)
        g_error_free(error);
      g_key_file_free(keyfile);
      goto GSLAPT_READ_CONFIG_END;
    }

    gslapt_settings->x      = g_key_file_get_integer (keyfile, "main window", "x",      NULL);
    gslapt_settings->y      = g_key_file_get_integer (keyfile, "main window", "y",      NULL);
    gslapt_settings->width  = g_key_file_get_integer (keyfile, "main window", "width",  NULL);
    gslapt_settings->height = g_key_file_get_integer (keyfile, "main window", "height", NULL);

    gslapt_settings->cl_x      = g_key_file_get_integer (keyfile, "changelog window", "x",      NULL);
    gslapt_settings->cl_y      = g_key_file_get_integer (keyfile, "changelog window", "y",      NULL);
    gslapt_settings->cl_width  = g_key_file_get_integer (keyfile, "changelog window", "width",  NULL);
    gslapt_settings->cl_height = g_key_file_get_integer (keyfile, "changelog window", "height", NULL);

    gslapt_settings->pref_x      = g_key_file_get_integer (keyfile, "preferences window", "x",      NULL);
    gslapt_settings->pref_y      = g_key_file_get_integer (keyfile, "preferences window", "y",      NULL);
    gslapt_settings->pref_width  = g_key_file_get_integer (keyfile, "preferences window", "width",  NULL);
    gslapt_settings->pref_height = g_key_file_get_integer (keyfile, "preferences window", "height", NULL);

    gslapt_settings->tran_x      = g_key_file_get_integer (keyfile, "transaction window", "x",      NULL);
    gslapt_settings->tran_y      = g_key_file_get_integer (keyfile, "transaction window", "y",      NULL);
    gslapt_settings->tran_width  = g_key_file_get_integer (keyfile, "transaction window", "width",  NULL);
    gslapt_settings->tran_height = g_key_file_get_integer (keyfile, "transaction window", "height", NULL);

    g_key_file_free(keyfile);

  }

GSLAPT_READ_CONFIG_END:
  free(file);

  return gslapt_settings;
}

void gslapt_free_rc(GslaptSettings *gslapt_settings)
{
  g_slice_free(GslaptSettings,gslapt_settings);
}

int gslapt_write_rc(GslaptSettings *gslapt_settings)
{
  char *dir;
  int rc = -1;

  if (gslapt_settings == NULL)
    return rc;

  dir = gslapt_init_rc_dir();
  if (dir != NULL) {
    gsize length;
    GKeyFile *keyfile= NULL;
    gchar *rc_data = NULL;

    keyfile = g_key_file_new();

    g_key_file_set_integer (keyfile, "main window", "x",      gslapt_settings->x);
    g_key_file_set_integer (keyfile, "main window", "y",      gslapt_settings->y);
    g_key_file_set_integer (keyfile, "main window", "width",  gslapt_settings->width);
    g_key_file_set_integer (keyfile, "main window", "height", gslapt_settings->height);

    g_key_file_set_integer (keyfile, "changelog window", "x",      gslapt_settings->cl_x);
    g_key_file_set_integer (keyfile, "changelog window", "y",      gslapt_settings->cl_y);
    g_key_file_set_integer (keyfile, "changelog window", "width",  gslapt_settings->cl_width);
    g_key_file_set_integer (keyfile, "changelog window", "height", gslapt_settings->cl_height);

    g_key_file_set_integer (keyfile, "preferences window", "x",      gslapt_settings->pref_x);
    g_key_file_set_integer (keyfile, "preferences window", "y",      gslapt_settings->pref_y);
    g_key_file_set_integer (keyfile, "preferences window", "width",  gslapt_settings->pref_width);
    g_key_file_set_integer (keyfile, "preferences window", "height", gslapt_settings->pref_height);

    g_key_file_set_integer (keyfile, "transaction window", "x",      gslapt_settings->tran_x);
    g_key_file_set_integer (keyfile, "transaction window", "y",      gslapt_settings->tran_y);
    g_key_file_set_integer (keyfile, "transaction window", "width",  gslapt_settings->tran_width);
    g_key_file_set_integer (keyfile, "transaction window", "height", gslapt_settings->tran_height);

    rc_data = g_key_file_to_data(keyfile, &length, NULL);
    if (length != 0) {
      char *file = g_build_path (G_DIR_SEPARATOR_S, dir, "rc", NULL);
      if (file != NULL) {
        FILE *fh = fopen(file,"wb");
        if (fh != NULL) {
          fprintf(fh, "%s", rc_data);
          rc = 0;
          fclose(fh);
        }
        g_free(file);
      }
    }

    if (rc_data != NULL)
      g_free(rc_data);

    g_key_file_free(keyfile);
    g_free(dir);
  }

  return rc;
}

