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

typedef struct background_process
{
  pid_t pid;
  unsigned int finished;
  SEND_CONTEXT *sctx;

  struct background_process *next;
} BACKGROUND_PROCESS;

static BACKGROUND_PROCESS *ProcessList = NULL;

static BACKGROUND_PROCESS *bg_process_new (pid_t pid, SEND_CONTEXT *sctx)
{
  BACKGROUND_PROCESS *process;

  process = safe_calloc (1, sizeof(BACKGROUND_PROCESS));
  process->pid = pid;
  process->sctx = sctx;

  return process;
}

static void bg_process_free (BACKGROUND_PROCESS **process)
{
  if (!process || !*process)
    return;

  /* The SEND_CONTEXT is managed independently of the process.
   * Don't free it here */
  FREE (process);   /* __FREE_CHECKED__ */
}

static void process_list_add (BACKGROUND_PROCESS *process)
{
  process->next = ProcessList;
  ProcessList = process;
  BackgroundProcessCount++;
}

static void process_list_remove (BACKGROUND_PROCESS *process)
{
  BACKGROUND_PROCESS *cur = ProcessList;
  BACKGROUND_PROCESS **plast = &ProcessList;

  while (cur)
  {
    if (cur == process)
    {
      *plast = cur->next;
      cur->next = NULL;
      BackgroundProcessCount--;
      break;
    }
    plast = &cur->next;
    cur = cur->next;
  }
}

int mutt_background_has_backgrounded (void)
{
  return ProcessList ? 1 : 0;
}

/* Returns 0 if no processes were updated to finished.
 *         1 if one or more processes finished
 */
int mutt_background_process_waitpid (void)
{
  BACKGROUND_PROCESS *process;
  pid_t pid;
  int has_finished = 0;

  if (!ProcessList)
    return 0;

  while ((pid = waitpid (-1, NULL, WNOHANG)) > 0)
  {
    process = ProcessList;
    while (process)
    {
      if (process->pid == pid)
      {
        process->finished = 1;
        has_finished = 1;
        break;
      }
      process = process->next;
    }
  }

  return has_finished;
}

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


/* Landing Page */

static void landing_redraw (MUTTMENU *menu)
{
  int row, col;
  char key[SHORT_STRING];
  BUFFER *messagebuf;
  size_t messagelen;

  menu_redraw (menu);

  if (MuttIndexWindow->rows < 2)
    return;

  messagebuf = mutt_buffer_pool_get ();

  /* L10N:
     Background Edit Landing Page message, first line.
     Displays while the editor is running.
  */
  mutt_buffer_strcpy (messagebuf, _("Waiting for editor to exit"));
  messagelen = mutt_buffer_len (messagebuf);
  row = MuttIndexWindow->rows >= 10 ? 5 : 0;
  col = (MuttIndexWindow->cols > messagelen) ?
    ((MuttIndexWindow->cols - messagelen) / 2) : 0;
  mutt_window_mvaddstr (MuttIndexWindow, row, col, mutt_b2s (messagebuf));

  *key = '\0';
  if (!km_expand_key (key, sizeof(key), km_find_func (MENU_GENERIC, OP_EXIT)))
    strfcpy (key, "<exit>", sizeof(key));

  /* L10N:
     Background Edit Landing Page message, second line.
     Displays while the editor is running.
     %s is the key binding for "<exit>", usually "q".
  */
  mutt_buffer_printf (messagebuf, _("Type '%s' to background compose session."),
                                    key);
  messagelen = mutt_buffer_len (messagebuf);
  row = MuttIndexWindow->rows >= 10 ? 6 : 1;
  col = (MuttIndexWindow->cols > messagelen) ?
    ((MuttIndexWindow->cols - messagelen) / 2) : 0;
  mutt_window_mvaddstr (MuttIndexWindow, row, col, mutt_b2s (messagebuf));

  mutt_buffer_pool_release (&messagebuf);
  mutt_curs_set (0);
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
  BACKGROUND_PROCESS *process;

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
    process = bg_process_new (pid, sctx);
    process_list_add (process);
  }

cleanup:
  mutt_buffer_pool_release (&cmd);
  return rc;
}


/* Background Compose Menu */

typedef struct entry
{
  int num;
  BACKGROUND_PROCESS *process;
} BG_ENTRY;

static const struct mapping_t BgComposeHelp[] = {
  { N_("Exit"),   OP_EXIT },
  /* L10N: Background Compose Menu Help line:
     resume composing the mail
  */
  { N_("Resume"),   OP_GENERIC_SELECT_ENTRY },
  { N_("Help"),   OP_HELP },
  { NULL,	  0 }
};

static const char *bg_format_str (char *dest, size_t destlen, size_t col,
                                  int cols, char op, const char *src,
                                  const char *fmt, const char *ifstring,
                                  const char *elsestring,
                                  void *data, format_flag flags)
{
  BG_ENTRY *entry = (BG_ENTRY *)data;
  SEND_CONTEXT *sctx = entry->process->sctx;
  HEADER *hdr = sctx->msg;
  char tmp[SHORT_STRING];
  char buf[LONG_STRING];
  const char *msgid;
  int optional = (flags & MUTT_FORMAT_OPTIONAL);

  switch (op)
  {
    case 'i':
      msgid = sctx->cur_message_id;
      if (!msgid && sctx->tagged_message_ids)
        msgid = sctx->tagged_message_ids->data;
      if (!optional)
        mutt_format_s (dest, destlen, fmt, NONULL (msgid));
      else if (!msgid)
        optional = 0;
      break;
    case 'n':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, entry->num);
      break;
    case 'p':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, entry->process->pid);
      break;
    case 'r':
      buf[0] = 0;
      rfc822_write_address(buf, sizeof(buf), hdr->env->to, 1);
      if (optional && buf[0] == '\0')
        optional = 0;
      mutt_format_s (dest, destlen, fmt, buf);
      break;
    case 'R':
      buf[0] = 0;
      rfc822_write_address(buf, sizeof(buf), hdr->env->cc, 1);
      if (optional && buf[0] == '\0')
        optional = 0;
      mutt_format_s (dest, destlen, fmt, buf);
      break;
    case 's':
      mutt_format_s (dest, destlen, fmt, NONULL (hdr->env->subject));
      break;
    case 'S':
      if (!optional)
      {
        if (entry->process->finished)
          /* L10N:
             Background Compose menu
             flag that indicates the editor process has finished.
          */
          mutt_format_s (dest, destlen, fmt, _("finished"));
        else
          /* L10N:
             Background Compose menu
             flag that indicates the editor process is still running.
          */
          mutt_format_s (dest, destlen, fmt, _("running"));
      }
      else if (!entry->process->finished)
        optional = 0;
      break;
  }

  if (optional)
    mutt_FormatString (dest, destlen, col, cols, ifstring, bg_format_str, entry, flags);
  else if (flags & MUTT_FORMAT_OPTIONAL)
    mutt_FormatString (dest, destlen, col, cols, elsestring, bg_format_str, entry, flags);

  return (src);
}

static void make_bg_entry (char *s, size_t slen, MUTTMENU *m, int num)
{
  BG_ENTRY *entry = &((BG_ENTRY *) m->data)[num];

  mutt_FormatString (s, slen, 0, MuttIndexWindow->cols,
                     NONULL (BackgroundFormat),
                     bg_format_str,
		     entry, MUTT_FORMAT_ARROWCURSOR);
}

static void update_bg_menu (MUTTMENU *menu)
{
  if (SigChld)
  {
    SigChld = 0;
    if (mutt_background_process_waitpid ())
      menu->redraw |= REDRAW_INDEX;
  }
}

static MUTTMENU *create_bg_menu ()
{
  MUTTMENU *menu = NULL;
  BACKGROUND_PROCESS *process;
  BG_ENTRY *entries = NULL;
  int num_entries = 0, i;
  char *helpstr;

  process = ProcessList;
  while (process)
  {
    num_entries++;
    process = process->next;
  }

  menu = mutt_new_menu (MENU_GENERIC);
  menu->make_entry = make_bg_entry;
  menu->custom_menu_update = update_bg_menu;

  /* L10N:
     Background Compose Menu title
  */
  menu->title = _("Background Compose Menu");
  helpstr = safe_malloc (STRING);
  menu->help = mutt_compile_help (helpstr, STRING, MENU_GENERIC,
                                  BgComposeHelp);

  menu->data = entries = safe_calloc (num_entries, sizeof(BG_ENTRY));
  menu->max = num_entries;

  process = ProcessList;
  i = 0;
  while (process)
  {
    entries[i].num = i + 1;
    entries[i].process = process;

    process = process->next;
    i++;
  }

  mutt_push_current_menu (menu);

  return menu;
}

static void free_bg_menu (MUTTMENU **menu)
{
  mutt_pop_current_menu (*menu);
  FREE (&(*menu)->data);
  FREE (&(*menu)->help);
  mutt_menuDestroy (menu);
}

void mutt_background_compose_menu (void)
{
  MUTTMENU *menu;
  int done = 0, op;
  BG_ENTRY *entry;
  BACKGROUND_PROCESS *process;
  SEND_CONTEXT *sctx;
  char msg[SHORT_STRING];

  if (!ProcessList)
  {
    /* L10N:
       Background Compose Menu:
       displayed if there are no background processes and the
       user tries to bring up the background compose menu
    */
    mutt_message _("No backgrounded editing sessions.");
    return;
  }

  /* Force a rescan, just in case somehow the signal was missed. */
  SigChld = 1;
  mutt_background_process_waitpid ();

  /* If there is only one process and it's finished, skip the menu */
  if (!ProcessList->next && ProcessList->finished)
  {
    process = ProcessList;
    sctx = process->sctx;
    process_list_remove (process);
    bg_process_free (&process);
    mutt_send_message_resume (&sctx);
    return;
  }

  menu = create_bg_menu ();
  while (!done)
  {
    switch ((op = mutt_menuLoop (menu)))
    {
      case OP_EXIT:
        done = 1;
        break;

      case OP_GENERIC_SELECT_ENTRY:
        if (menu->data)
        {
          entry = (BG_ENTRY *)(menu->data) + menu->current;
          process = entry->process;
          sctx = process->sctx;

          if (!process->finished)
          {
            snprintf (msg, sizeof(msg),
                      /* L10N:
                         Background Compose menu:
                         Confirms if an unfinished process is selected
                         to continue.
                      */
                      _("Process is still running. Really select?"));
            if (mutt_yesorno (msg, MUTT_NO) != MUTT_YES)
              break;
            mutt_message _("Waiting for editor to exit");
            waitpid (process->pid, NULL, 0);
            mutt_clear_error ();
          }

          process_list_remove (process);
          bg_process_free (&process);

          mutt_send_message_resume (&sctx);

          if (!ProcessList)
          {
            done = 1;
            break;
          }

          free_bg_menu (&menu);
          menu = create_bg_menu ();
        }
        break;
    }
  }

  free_bg_menu (&menu);
}
