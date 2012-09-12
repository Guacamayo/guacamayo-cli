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

#include "hostname.h"

gboolean
set_hostname (char *line)
{
  gboolean retval = TRUE;
  char *p = line;
  char *n;
  int   i, j, len;

  /* skip command and any traling space */
  while (*p && !isspace (*p))
    p++;

  while (*p && isspace (*p))
    p++;

  if (!*p)
    {
      char buf[256];

      if (gethostname (buf, sizeof(buf)))
        {
          g_print ("Failed to get hostname: %s\n", strerror (errno));
          return FALSE;
        }

      g_print ("Hostname: %s\n", buf);
      return TRUE;
    }

  n = g_strstrip (p);

  /*
   * Strip control chars
   */
  len = strlen (n);
  for (i = 0, j = 0; i < len; i++)
    if (n[i] >= ' ')
      n[j++] = n[i];

  n[j] = 0;

  if (sethostname (n, j))
    {
      g_print ("Failed to set hostname to '%s': %s\n", n, strerror (errno));
      retval = FALSE;
    }
  else
    {
      FILE *f;

      /*
       * The change made by sethostname() is not persistent, since at bootime
       * the hostname is read from /etc/hostname, so fix that too.
       */
      g_print ("Host name set to '%s'\n", n);

      if (!(f = fopen ("/etc/hostname", "w")))
        {
          g_print ("Failed to set save hostname: %s\n", strerror (errno));
          retval = FALSE;
        }
      else
        {
          if (fwrite (n, j, 1, f) != 1)
            {
              g_print ("Failed to set save hostname: %s\n", strerror (errno));
              retval = FALSE;
            }

          fclose (f);
        }
    }

  return retval;
}
