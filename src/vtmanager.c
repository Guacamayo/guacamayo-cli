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

#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>

static int  vt        = -1;
static int  ttyX      = -1;
static char *ttydev   = NULL;

/*
 * Decluttering macros for ioctl:
 *
 * _XIOCTL_W: Execute ioctl and print warning on failure
 * _XIOCTL_F: Execute ioctl and jump to 'fail' on failure
 */
#define _XIOCTL_W(err0, fd, io ,args...)                        \
  if (ioctl (fd, io, args) < 0)                                 \
    {                                                           \
      g_warning ("Failed to " err0 ":%s", strerror(errno));     \
    }                                                           \

#define _XIOCTL_F(err0, fd, io ,args...)                        \
  if (ioctl (fd, io, args) < 0)                                 \
    {                                                           \
      g_warning ("Failed to " err0 ":%s", strerror (errno));    \
      goto fail;                                                \
    }                                                           \

/*
 * Signal handler for VT switching:
 *   - SIGUSR1 when we switch to our VT
 *   - SIGUSR2 when we switch away from our VT
 */
static void
signal_handler (int id)
{
  switch (id)
    {
    case SIGUSR1:
      /* VT activate request -- VT_ACKACQ for allow and redraw */
      _XIOCTL_W ("switch to VT", ttyX, VT_RELDISP, VT_ACKACQ);
      break;
    case SIGUSR2:
      /* Deactivate VT request -- 1 for allow*/
      _XIOCTL_W ("release VT", ttyX, VT_RELDISP, 1);
      break;
    default:
      g_warning ("Got unknown signal %d", id);
    }
}

const char *
vtmanager_init (void)
{
  int               tty0 = -1;
  struct vt_mode    mode = {0};
  struct sigaction  sig;

  if ((tty0 = open ("/dev/tty0", O_WRONLY)) < 0)
    {
      g_warning ("Failed to open tty0");
      goto fail;
    }

  /*
   * Find an available VT
   */
  _XIOCTL_F ("locate free VT", tty0, VT_OPENQRY, &vt);
  if (vt < 0)
    {
      g_warning ("Invalid VT number %d", vt);
      goto fail;
    }

  /*
   * Detach from controlling tty and attach to the new VT instead (this
   * is necessary to be able to switch to the new VT without being root).
   */
  setsid ();
  ttydev = g_strdup_printf ("/dev/tty%d", vt);
  if ((ttyX = open (ttydev, O_RDWR|O_NONBLOCK)) < 0)
    {
      g_warning ("Failed to open '%s': %s", ttydev, strerror (errno));
      goto fail;
    }
  _XIOCTL_F ("change controlling tty", ttyX, TIOCSCTTY, 0);

  /*
   * Install our signal handler and switch over to process mode
   */
  sig.sa_handler = signal_handler;
  sig.sa_flags   = 0;
  sigemptyset (&sig.sa_mask);
  if (sigaction (SIGUSR1, &sig, NULL) < 0)
    {
      g_warning ("Failed to install SIGUSR1 handler");
      goto fail;
    }

  if (sigaction (SIGUSR2, &sig, NULL) < 0)
    {
      g_warning ("Failed to install SIGUSR2 handler");
      goto fail;
    }

  mode.mode = VT_PROCESS;
  mode.relsig = SIGUSR2;
  mode.acqsig = SIGUSR1;
  _XIOCTL_F ("set vt mode", ttyX, VT_SETMODE, &mode);

  goto done;

 fail:
  g_free (ttydev);
  ttydev = NULL;

  if (ttyX >= 0)
    {
      close (ttyX);
      ttyX = -1;
      vt = -1;
    }

 done:
  if (tty0 >= 0)
    close (tty0);

  return ttydev;
}

void
vtmanager_deinit ()
{
  int               tty0 = -1;
  struct vt_mode    mode = {0};
  struct sigaction  sig;

  if (vt < 0 || ttyX < 0)
    return;

  /* Switch console back to auto mode and remove signal handlers */
  mode.mode = VT_AUTO;
  mode.relsig = 0;
  mode.acqsig = 0;
  _XIOCTL_W ("set vt mode", ttyX, VT_SETMODE, &mode);

  sig.sa_handler = SIG_IGN;
  sig.sa_flags   = 0;
  sigemptyset (&sig.sa_mask);

  if (sigaction (SIGUSR1, &sig, NULL) < 0)
    g_warning ("Failed to remove SIGUSR1 handler");

  if (sigaction (SIGUSR2, &sig, NULL) < 0)
    g_warning ("Failed to remove SIGUSR2 handler");

  close (ttyX);
  ttyX = -1;

  if ((tty0 = open ("/dev/tty0", O_WRONLY)) < 0)
    g_warning ("Failed to open tty0");
  else
    {
      _XIOCTL_W ("disallocate", tty0, VT_DISALLOCATE, vt);
      close (tty0);
    }
}

void
vtmanager_activate (void)
{
  if (vt < 0 || ttyX < 0)
    return;

  _XIOCTL_F ("activate VT", ttyX, VT_ACTIVATE, vt);
  _XIOCTL_F ("wait for VT", ttyX, VT_WAITACTIVE, vt);

 fail:;
}
