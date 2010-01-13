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
} GslaptSettings;

char *gslapt_init_rc_dir (void);
GslaptSettings *gslapt_new_rc (void);
GslaptSettings *gslapt_read_rc (void);
int gslapt_write_rc(GslaptSettings *gslapt_settings);
void gslapt_free_rc(GslaptSettings *gslapt_settings);

#endif
