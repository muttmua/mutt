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
#include "mutt_menu.h"
#include "send.h"
#include "background.h"

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>


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

static const struct mapping_t LandingHelp[] = {
  { N_("Exit"),  OP_EXIT },
  { N_("Redraw"), OP_REDRAW },
  { N_("Help"),  OP_HELP },
  { NULL,	 0 }
};

static void landing_redraw (MUTTMENU *menu)
{
  menu_redraw (menu);
  mutt_window_mvaddstr (MuttIndexWindow, 0, 0,
                        _("Waiting for editor to exit"));
  mutt_window_mvaddstr (MuttIndexWindow, 1, 0,
                        _("Hit <exit> to background editor."));
}

/* Displays the "waiting for editor" page.
 * Returns:
 *   2 if the the menu is exited, leaving the process backgrounded
 *   0 when the waitpid() indicates the process has exited
 */
static int background_edit_landing_page (pid_t bg_pid)
{
  int done = 0, rc = 0, op;
  short orig_timeout;
  pid_t wait_rc;
  MUTTMENU *menu;
  char helpstr[STRING];

  menu = mutt_new_menu (MENU_GENERIC);
  menu->help = mutt_compile_help (helpstr, sizeof(helpstr),
                                  MENU_GENERIC, LandingHelp);
  menu->pagelen = 0;
  menu->title = _("Waiting for editor to exit");

  mutt_push_current_menu (menu);

  /* Reduce timeout so we poll with bg_pid every second */
  orig_timeout = Timeout;
  Timeout = 1;

  while (!done)
  {
    wait_rc = waitpid (bg_pid, NULL, WNOHANG);
    if ((wait_rc > 0) ||
        ((wait_rc < 0) && (errno == ECHILD)))
    {
      rc = 0;
      break;
    }

#if defined (USE_SLANG_CURSES) || defined (HAVE_RESIZETERM)
    if (SigWinch)
    {
      SigWinch = 0;
      mutt_resize_screen ();
      clearok (stdscr, TRUE);
    }
#endif

    if (menu->redraw)
      landing_redraw (menu);

    op = km_dokey (MENU_GENERIC);

    switch (op)
    {
      case OP_HELP:
        mutt_help (MENU_GENERIC);
        menu->redraw = REDRAW_FULL;
        break;

      case OP_EXIT:
        rc = 2;
        done = 1;
        break;

      case OP_REDRAW:
	clearok (stdscr, TRUE);
        menu->redraw = REDRAW_FULL;
        break;
    }
  }

  Timeout = orig_timeout;

  mutt_pop_current_menu (menu);
  mutt_menuDestroy (&menu);

  return rc;
}

/* Runs editor in the background.
 *
 * After backgrounding the process, the background landing page will
 * be displayed.  The user will have the opportunity to "quit" the
 * landing page, exiting back to the index.  That will return 2
 * (chosen for consistency with other backgrounding functions).
 *
 * If they leave the landing page up, it will detect when the editor finishes
 * and return 0, indicating the callers should continue processing
 * as if it were a foreground edit.
 *
 * Returns:
 *      2  - the edit was backgrounded
 *      0  - background edit completed.
 *     -1  - an error occurred
 */
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

  rc = background_edit_landing_page (pid);
  if (rc == 2)
  {
    sctx->background_pid = pid;
    BackgroundProcess = sctx;
  }

cleanup:
  mutt_buffer_pool_release (&cmd);
  return rc;
}
