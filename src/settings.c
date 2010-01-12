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
  g->x = g->y = g->width = g->height = 0;

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

    gslapt_settings->x      = g_key_file_get_integer (keyfile, "window", "x",      NULL);
    gslapt_settings->y      = g_key_file_get_integer (keyfile, "window", "y",      NULL);
    gslapt_settings->width  = g_key_file_get_integer (keyfile, "window", "width",  NULL);
    gslapt_settings->height = g_key_file_get_integer (keyfile, "window", "height", NULL);

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

    g_key_file_set_integer (keyfile, "window", "x",      gslapt_settings->x);
    g_key_file_set_integer (keyfile, "window", "y",      gslapt_settings->y);
    g_key_file_set_integer (keyfile, "window", "width",  gslapt_settings->width);
    g_key_file_set_integer (keyfile, "window", "height", gslapt_settings->height);

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

