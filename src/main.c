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

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <glib.h>

#ifdef HAVE_GUACAMAYO_VERSION_H
#include <guacamayo-version.h>
#else
#define GUACAMAYO_DISTRO_STRING "Guacamayo"
#endif

static void
print_help (void)
{
  g_print ("Available Commands:\n\n"
           "  help: print this help message\n"
           "     ?: same as help\n"
#ifdef DEBUG
           "  quit: quit client\n"
#endif
           );
}

static gboolean
parse_line (char *line)
{
  char   *l = g_strstrip (line);
  char   *p = l;
  size_t  n = -1;

  switch (*l)
    {
#ifdef DEBUG
    case 'q':
      return TRUE;
#endif
    case '?':
      print_help();
      break;
    default:;
    }

  while (*p && !isspace (*p))
    p++;

  n = p - l;


  if (!strncmp ("help", line, n))
    {
      print_help ();
    }

  /* remove duplicates from history */
  if (history_search_prefix (line, -1) >= 0)
    {
      int          pos = where_history ();
      HIST_ENTRY * e;

      if ((e = remove_history (pos)))
        free_history_entry (e);
    }

  add_history (line);

  return FALSE;
}

int
main (int argc, char **argv)
{
  int               rows, cols;
  const char       *home;
  char             *history_file = NULL;
  char             *line;

  rl_initialize();
  rl_get_screen_size (&rows, &cols);

  if ((home = getenv ("HOME")))
    {
      using_history ();

      history_file = g_build_filename (home, ".guaca-cli-history", NULL);
      read_history (history_file);
    }

  g_print ("Welcome to " GUACAMAYO_DISTRO_STRING "\n");

  while ((line = readline (": ")))
    {
      if (parse_line (line))
        break;

      free (line);
    }

  write_history (history_file);
  g_free (history_file);
}

