#ifndef GSLAPT_SERIES_HEADER
#define GSLAPT_SERIES_HEADER

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include "config.h"

GHashTable *gslapt_series_map_init(void);
void gslapt_series_map_free(GHashTable *map);
int gslapt_series_map_fill(GHashTable *map);
char *gslapt_series_map_lookup(GHashTable *map, const char *key);

#endif
