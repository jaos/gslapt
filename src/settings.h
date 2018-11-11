/*
 * Copyright (C) 2003-2018 Jason Woodward <woodwardj at jaos dot org>
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

#ifndef GSLAPT_SETTINGS_HEADER
#define GSLAPT_SETTINGS_HEADER

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include "config.h"

typedef struct {
  gint x; gint y; gint width; gint height;
  gint cl_x; gint cl_y; gint cl_width; gint cl_height;
  gint pref_x; gint pref_y; gint pref_width; gint pref_height;
  gint tran_x; gint tran_y; gint tran_width; gint tran_height;
} GslaptSettings;

char *gslapt_init_rc_dir (void);
GslaptSettings *gslapt_new_rc (void);
GslaptSettings *gslapt_read_rc (void);
int gslapt_write_rc(GslaptSettings *gslapt_settings);
void gslapt_free_rc(GslaptSettings *gslapt_settings);

#endif
