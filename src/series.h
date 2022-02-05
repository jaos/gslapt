/*
 * Copyright (C) 2003-2022 Jason Woodward <woodwardj at jaos dot org>
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

#ifndef GSLAPT_SERIES_HEADER
#define GSLAPT_SERIES_HEADER

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>

GHashTable *gslapt_series_map_init(void);
void gslapt_series_map_free(GHashTable *map);
int gslapt_series_map_fill(GHashTable *map);
char *gslapt_series_map_lookup(GHashTable *map, const char *key);

#endif
