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

#include "series.h"

GHashTable *gslapt_series_map_init(void)
{
    GHashTable *map = g_hash_table_new(g_str_hash, g_str_equal);

    return map;
}

void gslapt_series_map_free(GHashTable *map)
{
    g_hash_table_destroy(map);
}

int gslapt_series_map_fill(GHashTable *map)
{
    char *file = g_build_path(G_DIR_SEPARATOR_S, PACKAGE_DATA_DIR, PACKAGE, "series_map.rc", NULL);
    if (file == NULL) {
        return -1;
    }

    if (g_file_test(file, G_FILE_TEST_IS_REGULAR) == TRUE) {
        GKeyFile *keyfile = NULL;
        GKeyFileFlags flags = G_KEY_FILE_NONE;
        GError *error = NULL;

        keyfile = g_key_file_new();
        if (keyfile == NULL)
            goto GSLAPT_SERIES_MAP_FILL_END;

        if (!g_key_file_load_from_file(keyfile, file, flags, &error)) {
            if (error != NULL)
                g_error_free(error);
            if (keyfile != NULL)
                g_key_file_free(keyfile);
            goto GSLAPT_SERIES_MAP_FILL_END;
        }

        gchar **keys = g_key_file_get_keys(keyfile, "series mappings", NULL, NULL);
        if (keys != NULL) {
            for (int c = 0; keys[c] != NULL; c++) {
                gchar *value = g_key_file_get_locale_string(keyfile, "series mappings", keys[c], NULL, NULL);
                if (value != NULL) {
                    g_hash_table_insert(map, g_strdup(keys[c]), g_strdup(value));
                    g_free(value);
                    g_free(keys[c]);
                }
            }
            g_free(keys);
        }

        g_key_file_free(keyfile);
    }

GSLAPT_SERIES_MAP_FILL_END:
    free(file);

    return 0;
}

char *gslapt_series_map_lookup(GHashTable *map, const char *key)
{
    if (key == NULL) {
        return NULL;
    }

    char *value = NULL;
    void *v = g_hash_table_lookup(map, key);
    if (v != NULL) {
        value = (char *)v;
    } else {
        if (strcmp(key, "") != 0)
            value = g_path_get_basename(key);
    }

    if (value != NULL) {
        char *converted = g_convert(value, strlen(value), "UTF-8", "UTF-8", NULL, NULL, NULL);
        if (converted != NULL)
            value = converted;
    }

    return value;
}
