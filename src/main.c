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

#include <stdarg.h>
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

#include "main.h"
#include "hostname.h"
#include "timezone.h"
#include "connman.h"
#include "vtmanager.h"

static FILE      *out = NULL;
static FILE      *in  = NULL;
static char      *history_file = NULL;
static GMainLoop *loop = NULL;

static gboolean   print_help (char *line);
static gboolean   print_version (char *line);
static gboolean   shutdown (char *line);

typedef gboolean (*GuacaCmdFunc) (char *);

void
output (const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);

  if (out)
    vfprintf (out, fmt, args);
  else
    vfprintf (stdout, fmt, args);

  va_end (args);
}

/*
 * C_DEBUG:  only shown in help in debug build, but no restrictions on
 *           availability per se.
 *
 * C_HIDDEN: never shown in help output
 *
 * C_SHELL:  in non-debug build only available if running under shell; in
 *           debug build all commands are always available.
 */
enum
{
  C_NONE   = 0,
  C_DEBUG  = 0x00000001,
  C_HIDDEN = 0x00000002,
  C_SHELL  = 0x00000004,
};

typedef struct
{
  const char   *cmd;
  const char   *args;
  const char   *help;
  GuacaCmdFunc  func;
  guint         flags;
} GuacaCmd;

/*
 * Keep sorted !!!
 */
static GuacaCmd cmds[] =
{
  {"?",        NULL,         "Print help message", print_help,   C_HIDDEN},
  {"help",     NULL,         "Print help message", print_help,   C_NONE},
  {"hostname", "[new name]", "Get/set host name",  set_hostname, C_NONE},
  {"quit",     NULL,         "Quit",               NULL,         C_SHELL},
  {"reboot",   NULL,         "Reboot",             shutdown,     C_NONE},
  {"shutdown", NULL,         "Shutdown",           shutdown,     C_NONE},
  {"timezone", NULL,         "Set timezone",       set_timezone, C_NONE},
  {"version",  NULL,         "Guacamayo version",  print_version,C_NONE},
  {"wifi",     NULL,         "Connect to wifi",    setup_wifi,   C_NONE},
};

static gboolean
is_cmd_available (guint flags)
{
  gboolean visible;

  /* In debug mode all commands are available */
#ifdef DEBUG
  return TRUE;
#endif

  /* Commands with the SHELL flag are visible if we are in a shell */
  if ((flags & C_SHELL))
    return !out;

  return TRUE;
}

static gboolean
is_cmd_visible (guint flags)
{
  gboolean visible;

  /* Hidden commands are never visible. */
  if ((flags & C_HIDDEN))
    return FALSE;

  /* debug commands are only visible in debug mode */
#ifndef DEBUG
  if ((flags & C_DEBUG))
    return FALSE;
#endif

  return is_cmd_available (flags);
}

static gboolean
print_version (char *line)
{
#ifdef HAVE_GUACAMAYO_VERSION_H
  output (GUACAMAYO_DISTRO_STRING "\n");
#else
  output ("Sorry mate, this ain't a Guacamayo build.\n");
#endif
}

static gboolean
print_help (char *line)
{
  int i;
  static char fmt_str[20];
  static int max_len = 0;

  if (!max_len)
    {
      for (i = 0; i < G_N_ELEMENTS (cmds); i++)
        {
          int len;

          if (!is_cmd_visible (cmds[i].flags))
            continue;

          len = strlen (cmds[i].cmd);

          if (cmds[i].args)
            len += (strlen (cmds[i].args) + 1);

          max_len = MAX (max_len, len);
        }

      snprintf (fmt_str, sizeof(fmt_str), "    %%-%ds: %%s\n", max_len);
    }

  output ("Available Commands:\n\n");

  for (i = 0; i < G_N_ELEMENTS (cmds); i++)
    {
      if (!is_cmd_visible (cmds[i].flags))
        continue;

      if (cmds[i].args)
        {
          char *t = g_strdup_printf ("%s %s", cmds[i].cmd, cmds[i].args);
          output (fmt_str, t, cmds[i].help);
          g_free (t);
        }
      else
        output (fmt_str, cmds[i].cmd, cmds[i].help);
    }

  output ("\n");

  return TRUE;
}

static gboolean
shutdown (char *line)
{
  const char *param = "-r";
  char       *cmdline;
  GError     *error = NULL;
  gboolean    retval = TRUE;

  if (!strncmp (line, "shutdown", strlen ("shutdown")))
    param = "-h";

  cmdline = g_strdup_printf ("/sbin/shutdown %s now", param);

  if (!g_spawn_command_line_async (cmdline, &error))
    {
      output ("Failed to execute '%s': %s\n", cmdline, error->message);
      g_clear_error (&error);
      retval = FALSE;
    }

  g_free (cmdline);
  return retval;
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
    case 0:
      /* print help on empty lines */
    case '?':
      print_help (line);
      return FALSE;
    default:;
    }

  while (*p && !isspace (*p))
    p++;

  n = p - l;

  for (i = 0; i < G_N_ELEMENTS (cmds); i++)
    if (!strncmp (cmds[i].cmd, line, n))
      {
        if (!is_cmd_available (cmds[i].flags))
          {
            output ("Sorry mate, can't let you do that.\n");
            success = FALSE;
          }
        else
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
    if (out && !g_strcmp0 (cmds[i].cmd, "quit"))
      continue;
    else if (!strncmp (text, cmds[i].cmd, strlen (text)))
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

static void
signal_handler (int sig)
{
  switch (sig)
    {
    default:
      break;

    case SIGINT:
      /* Terminate any subcommands using the glib main loop. */
      g_main_loop_quit (loop);

      /*
       * disable SIGINT if we are running on a dedicated VT, otherwise fall
       * through
       */
      if (out)
        {
          output ("Sorry mate, can't let you do that.\n");
          return;
        }

    case SIGABRT:
    case SIGSEGV:
    case SIGTERM:
      /* Try to release the VT if at all possible */
      if (out)
        fclose (out);

      if (in)
        fclose (in);

      vtmanager_deinit ();

      if (history_file)
        write_history (history_file);
    }

  exit (sig);
}

GMainLoop *
get_main_loop (void)
{
  return loop;
}

int
main (int argc, char **argv)
{
  int               rows, cols;
  const char       *home;
  char             *line;
  const char       *tty;
  struct sigaction  sa;

  sigfillset(&sa.sa_mask);
  sa.sa_handler = signal_handler;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  tty = vtmanager_init ();
  vtmanager_activate ();

  g_type_init ();

  rl_initialize();

  if (tty)
    {
      if ((out = fopen (tty, "w")))
        {
          if (rl_outstream)
            fclose (rl_outstream);

          rl_outstream = out;
        }

      if ((in = fopen (tty, "r")))
        {
          if (rl_instream)
            fclose (rl_instream);

          rl_instream = in;
        }
    }

  rl_attempted_completion_function = guaca_completion;
  rl_get_screen_size (&rows, &cols);

  if ((home = getenv ("HOME")))
    {
      using_history ();

      history_file = g_build_filename (home, ".guaca-cli-history", NULL);
      read_history (history_file);
    }

  /* main loop for use in commnds */
  loop = g_main_loop_new (NULL, FALSE);

#ifdef HAVE_GUACAMAYO_VERSION_H
  output ("Welcome to " GUACAMAYO_DISTRO_STRING "\n\n");
#else
  output ("\n\n");
#endif

  print_help (NULL);

  while ((line = readline (": ")))
    {
      if (parse_line (line))
        break;

      free (line);
    }

  if (history_file)
    write_history (history_file);

  g_free (history_file);

  if (out)
    fclose (out);

  if (in)
    fclose (in);

  g_main_loop_unref (loop);

  vtmanager_deinit ();
}

