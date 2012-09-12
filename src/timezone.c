/*
 *  Copyright (C) 2012, sleep(5) ltd <http://sleepfive.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  Author: Tomas Frydrych <tomas@sleepfive.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <readline/readline.h>

#include "main.h"
#include "timezone.h"

#define PROMPT "timezone> "

typedef struct TzEntry
{
  char       *country;
  char       *zone;
  char       *region;
  char       *city;
} TzEntry;

static TzEntry *
tz_entry_new (const char *country, const char *zone)
{
  TzEntry     *t;
  char        *p, *region, *path;
  struct stat  st;

  /*
   * Make sure we have the actual zone info here, since Poky prunes the data
   * without prooning the zones.tab
   */
  path = g_build_filename ("/usr/share/zoneinfo", zone, NULL);

  if (stat (path, &st) < 0)
    {
      g_free (path);
      return NULL;
    }

  g_free (path);

  t = g_slice_new0 (TzEntry);

  t->country = g_strdup (country);
  t->zone    = g_strdup (zone);

  region = g_strdup (zone);

  /* replace underscores with spaces */
  for (p = region; *p; p++)
    if (*p == '_')
      *p = ' ';

  if ((p = strchr (region, '/')))
    {
      *p = 0;
      t->city = g_strdup (p+1);
    }

  t->region = g_strdup (region);

  g_free (region);

  return t;
}

static void
tz_entry_free (TzEntry *t)
{
  g_free (t->country);
  g_free (t->zone);
  g_free (t->region);
  g_free (t->city);

  g_slice_free (TzEntry, t);
}

static int
tz_entry_cmp (TzEntry *e1, TzEntry *e2)
{
  return g_strcmp0 (e1->zone, e2->zone);
}

static GHashTable *
get_zones (void)
{
  FILE  *f;
  char   buf[512];
  GList *regions;
  GList *l;

  static GHashTable *regions_tbl = NULL;

  if (regions_tbl)
    return regions_tbl;

  regions_tbl = g_hash_table_new (g_str_hash, g_str_equal);

  if (!(f = fopen ("/usr/share/zoneinfo/zone.tab", "r")))
    {
      g_warning ("Failed to open zone.tab: %s", strerror (errno));
      return FALSE;
    }

  while (fgets (buf, sizeof (buf), f))
    {
      char    *code, *coords, *zone;
      TzEntry *e;

      if (buf[0] == '#')
        continue;

      buf[sizeof (buf)-1] = 0;

      if (! (code = strtok (buf, "\t\n")))
        continue;
      if (! (coords = strtok (NULL, "\t\n")))
        continue;
      if (! (zone = strtok (NULL, "\t\n")))
        continue;

      /*
       * Push this into a hash table keyed by region (the MxComboBox is too
       * inefficient to manage big lists, and it would be user unfriendly anyway
       */
      if (!(e = tz_entry_new (code, zone)))
        continue;

      l = g_hash_table_lookup (regions_tbl, e->region);
      l = g_list_prepend (l, e);
      g_hash_table_insert (regions_tbl, e->region, l);
    }

  fclose (f);

  /*
   * Now sort out the individual sublists
   */
  regions = g_hash_table_get_values (regions_tbl);
  for (l = regions; l; l = l->next)
    {
      GList   *k = l->data;
      TzEntry *e = k->data;

      k = g_list_sort (k, (GCompareFunc)tz_entry_cmp);
      g_hash_table_insert (regions_tbl, e->region, k);
    }
  g_list_free (regions);

  return regions_tbl;
}

static gboolean
write_timezone (const char *zone)
{
  gboolean    retval = TRUE;
  char       *p;
  char       *path = NULL;
  FILE       *f = NULL;
  struct stat st;

  path = g_build_filename ("/usr/share/zoneinfo", zone, NULL);

  if (stat (path, &st) < 0)
    {
      output ("Failed to stat '%s': %s\n", zone, strerror (errno));
      retval = FALSE;
      goto finish;
    }

  if ((f = fopen ("/etc/timezone", "w")))
    {
      fputs (zone, f);
    }
  else
    {
      output ("Failed to open /etc/timezone: %s\n", strerror (errno));
      retval = FALSE;
      goto finish;
    }

  if (unlink ("/etc/localtime"))
    {
      output ("Failed to unlink localtime: %s\n", strerror (errno));
      retval = FALSE;
      goto finish;
    }


  if (symlink (path, "/etc/localtime"))
    {
      output ("Failed to symlink local time: %s\n", strerror (errno));
      retval = FALSE;
      goto finish;
    }

 finish:
  if (f)
    fclose (f);

  g_free (path);

  return retval;
}

gboolean
set_timezone (char *line)
{
  gboolean retval = TRUE;
  GHashTable *regions_tbl;
  GList      *keys, *l, *r;
  int         i;
  char       *sel = NULL;
  const char *key;
  TzEntry    *e;

  regions_tbl = get_zones ();
  keys = g_hash_table_get_keys (regions_tbl);
  keys = g_list_sort (keys, (GCompareFunc)g_strcmp0);

  for (i = 1, l = keys; l; l = l->next, i++)
    {
      const char *region = l->data;

      output (PROMPT "    %d: %s\n", i, region);
    }

  output (PROMPT "\n" PROMPT "Select regions [1-%d]\n", i-1);

  if (!(sel = readline (PROMPT "? ")))
    {
      retval = TRUE;
      goto finish;
    }

  if (!(i = strtol (sel, NULL, 10)))
    {
      retval = FALSE;
      goto finish;
    }

  key = g_list_nth_data (keys, i-1);

  if (!(r = g_hash_table_lookup (regions_tbl, key)))
    {
      retval = FALSE;
      goto finish;
    }

  for (i = 1, l = r; l; l = l->next, i++)
    {
      e = l->data;

      output (PROMPT "    %d: %s, %s\n", i, e->country, e->city);
    }

  output (PROMPT "\n" PROMPT "Select city [1-%d]\n", i-1);

  free (sel);

  if (!(sel = readline (PROMPT "? ")))
    {
      retval = TRUE;
      goto finish;
    }

  if (!(i = strtol (sel, NULL, 10)))
    {
      retval = TRUE;
      goto finish;
    }

  e = g_list_nth_data (r, i-1);

  retval = write_timezone (e->zone);

 finish:
  if (sel)
    free (sel);

  g_list_free (keys);

  return retval;
}
