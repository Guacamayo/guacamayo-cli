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
#include <unistd.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <glib.h>

#ifdef HAVE_GUACAMAYO_VERSION_H
#include <guacamayo-version.h>
#else
#define GUACAMAYO_DISTRO_STRING "Guacamayo"
#endif

#include "hostname.h"
#include "timezone.h"

static gboolean print_help (char *line);

typedef gboolean (*GuacaCmdFunc) (char *);

typedef struct
{
  const char   *cmd;
  const char   *help;
  GuacaCmdFunc  func;
} GuacaCmd;

static GuacaCmd cmds[] =
{
  {"?",         "Print help message", print_help},
  {"help",      "Print help message", print_help},
  {"hostname",  "Get/set host name",  set_hostname},
#ifdef DEBUG
  {"quit",      "Quit",               NULL},
#endif
  {"timezone",  "Set timezone",       set_timezone},
};

static gboolean
print_help (char *line)
{
  int i;
  static char fmt_str[20];
  static int max_len = 0;

  if (!max_len)
    {

      for (i = 0; i < G_N_ELEMENTS (cmds); i++)
        max_len = MAX (max_len, strlen (cmds[i].cmd));

      snprintf (fmt_str, sizeof(fmt_str), "%%-%ds: %%s\n", max_len);
    }

  g_print ("Available Commands:\n\n");

  for (i = 0; i < G_N_ELEMENTS (cmds); i++)
    g_printf (fmt_str, cmds[i].cmd, cmds[i].help);

  return TRUE;
}

static gboolean
parse_line (char *line)
{
  char   *l = g_strstrip (line);
  char   *p = l;
  size_t  n = -1;
  gboolean success = FALSE;
  int     i;

  switch (*l)
    {
#ifdef DEBUG
    case 'q':
      return TRUE;
#endif
    case '?':
      print_help (line);
      break;
    default:;
    }

  while (*p && !isspace (*p))
    p++;

  n = p - l;

  for (i = 0; i < G_N_ELEMENTS (cmds); i++)
    if (!strncmp (cmds[i].cmd, line, n))
      {
        success = cmds[i].func (line);
        break;
      }

  if (success)
    {
      /* remove duplicates from history */
      if (history_search_prefix (line, -1) >= 0)
        {
          int          pos = where_history ();
          HIST_ENTRY * e;

          if ((e = remove_history (pos)))
            free_history_entry (e);
        }

      add_history (line);
    }

  return FALSE;
}

static char *
command_match (const char *text, int state)
{
  static int i = 0;

  if (!state)
    i = 0;

  for (; i < G_N_ELEMENTS (cmds); i++)
    if (!strncmp (text, cmds[i].cmd, strlen (text)))
      return strdup (cmds[i++].cmd);

  return NULL;
}

static char **
guaca_completion (const char *text, int start, int end)
{
  char **m = NULL;

  if (start == 0)
    m = rl_completion_matches (text, command_match);

  return m;
}

int
main (int argc, char **argv)
{
  int               rows, cols;
  const char       *home;
  char             *history_file = NULL;
  char             *line;

  rl_initialize();
  rl_attempted_completion_function = guaca_completion;
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

