/*
 * Copyright (C) 1996-2000,2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "send.h"
#include "background.h"

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static pid_t mutt_background_run (const char *cmd)
{
  struct sigaction act;
  pid_t thepid;
  int fd;

  if (!cmd || !*cmd)
    return (0);

  /* must ignore SIGINT and SIGQUIT */
  mutt_block_signals_system ();

  if ((thepid = fork ()) == 0)
  {
    /* give up controlling terminal */
    setsid ();

    /* this ensures the child can't use stdin to take control of the
     * terminal */
#if defined(OPEN_MAX)
    for (fd = 0; fd < OPEN_MAX; fd++)
      close (fd);
#elif defined(_POSIX_OPEN_MAX)
    for (fd = 0; fd < _POSIX_OPEN_MAX; fd++)
      close (fd);
#else
    close (0);
    close (1);
    close (2);
#endif

    /* reset signals for the child; not really needed, but... */
    mutt_unblock_signals_system (0);
    act.sa_handler = SIG_DFL;
    act.sa_flags = 0;
    sigemptyset (&act.sa_mask);
    sigaction (SIGTERM, &act, NULL);
    sigaction (SIGTSTP, &act, NULL);
    sigaction (SIGCONT, &act, NULL);

    execle (EXECSHELL, "sh", "-c", cmd, NULL, mutt_envlist ());
    _exit (127); /* execl error */
  }

  /* reset SIGINT, SIGQUIT and SIGCHLD */
  mutt_unblock_signals_system (1);

  return (thepid);
}

int mutt_background_edit_file (SEND_CONTEXT *sctx, const char *editor,
                               const char *filename)
{
  BUFFER *cmd;
  pid_t pid;
  int rc = -1;

  cmd = mutt_buffer_pool_get ();

  mutt_expand_file_fmt (cmd, editor, filename);
  pid = mutt_background_run (mutt_b2s (cmd));
  if (pid <= 0)
  {
    mutt_error (_("Error running \"%s\"!"), mutt_b2s (cmd));
    mutt_sleep (2);
    goto cleanup;
  }

  sctx->background_pid = pid;
  BackgroundProcess = sctx;

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&cmd);
  return rc;
}
