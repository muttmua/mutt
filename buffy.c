/*
 * Copyright (C) 1996-2000,2010,2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2016-2017 Kevin J. McCarthy <kevin@8t8.us>
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
#include "buffy.h"
#include "mailbox.h"
#include "mx.h"

#include "mutt_curses.h"
#include "mutt_menu.h"

#ifdef USE_SIDEBAR
#include "sidebar.h"
#endif

#ifdef USE_IMAP
#include "imap.h"
#endif

#ifdef USE_INOTIFY
#include "monitor.h"
#endif

#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include <ctype.h>
#include <unistd.h>

#include <stdio.h>

static time_t BuffyTime = 0;	/* last time we started checking for mail */
static time_t BuffyStatsTime = 0; /* last time we check performed mail_check_stats */
time_t BuffyDoneTime = 0;	/* last time we knew for sure how much mail there was. */
static short BuffyCount = 0;	/* how many boxes with new mail */
static short BuffyNotify = 0;	/* # of unnotified new boxes */

static BUFFY* buffy_get (const char *path);

/* Find the last message in the file.
 * upon success return 0. If no message found - return -1 */

static int fseek_last_message (FILE * f)
{
  LOFF_T pos;
  char buffer[BUFSIZ + 9];	/* 7 for "\n\nFrom " */
  int bytes_read;
  int i;			/* Index into `buffer' for scanning.  */

  memset (buffer, 0, sizeof(buffer));
  fseek (f, 0, SEEK_END);
  pos = ftello (f);

  /* Set `bytes_read' to the size of the last, probably partial, buffer; 0 <
   * `bytes_read' <= `BUFSIZ'.  */
  bytes_read = pos % BUFSIZ;
  if (bytes_read == 0)
    bytes_read = BUFSIZ;
  /* Make `pos' a multiple of `BUFSIZ' (0 if the file is short), so that all
   * reads will be on block boundaries, which might increase efficiency.  */
  while ((pos -= bytes_read) >= 0)
  {
    /* we save in the buffer at the end the first 7 chars from the last read */
    strncpy (buffer + BUFSIZ, buffer, 5+2); /* 2 == 2 * mutt_strlen(CRLF) */
    fseeko (f, pos, SEEK_SET);
    bytes_read = fread (buffer, sizeof (char), bytes_read, f);
    if (bytes_read == -1)
      return -1;
    for (i = bytes_read; --i >= 0;)
      if (!mutt_strncmp (buffer + i, "\n\nFrom ", mutt_strlen ("\n\nFrom ")))
      {				/* found it - go to the beginning of the From */
	fseeko (f, pos + i + 2, SEEK_SET);
	return 0;
      }
    bytes_read = BUFSIZ;
  }

  /* here we are at the beginning of the file */
  if (!mutt_strncmp ("From ", buffer, 5))
  {
    fseek (f, 0, 0);
    return (0);
  }

  return (-1);
}

/* Return 1 if the last message is new */
static int test_last_status_new (FILE * f)
{
  HEADER *hdr;
  ENVELOPE* tmp_envelope;
  int result = 0;

  if (fseek_last_message (f) == -1)
    return (0);

  hdr = mutt_new_header ();
  tmp_envelope = mutt_read_rfc822_header (f, hdr, 0, 0);
  if (!(hdr->read || hdr->old))
    result = 1;

  mutt_free_envelope(&tmp_envelope);
  mutt_free_header (&hdr);

  return result;
}

static int test_new_folder (const char *path)
{
  FILE *f;
  int rc = 0;
  int typ;

  typ = mx_get_magic (path);

  if (typ != MUTT_MBOX && typ != MUTT_MMDF)
    return 0;

  if ((f = fopen (path, "rb")))
  {
    rc = test_last_status_new (f);
    safe_fclose (&f);
  }

  return rc;
}

void mutt_buffy_cleanup (const char *buf, struct stat *st)
{
#ifdef HAVE_UTIMENSAT
  struct timespec ts[2];
#else
  struct utimbuf ut;
#endif
  BUFFY *tmp;

  if (option(OPTCHECKMBOXSIZE))
  {
    tmp = mutt_find_mailbox (buf);
    if (tmp && !tmp->new)
      mutt_update_mailbox (tmp);
  }
  else
  {
    /* fix up the times so buffy won't get confused */
    if (st->st_mtime > st->st_atime)
    {
#ifdef HAVE_UTIMENSAT
      ts[0].tv_sec = 0;
      ts[0].tv_nsec = UTIME_OMIT;
      ts[1].tv_sec = 0;
      ts[1].tv_nsec = UTIME_NOW;
      utimensat (AT_FDCWD, buf, ts, 0);
#else
      ut.actime = st->st_atime;
      ut.modtime = time (NULL);
      utime (buf, &ut);
#endif
    }
    else
    {
#ifdef HAVE_UTIMENSAT
      ts[0].tv_sec = 0;
      ts[0].tv_nsec = UTIME_NOW;
      ts[1].tv_sec = 0;
      ts[1].tv_nsec = UTIME_NOW;
      utimensat (AT_FDCWD, buf, ts, 0);
#else
      utime (buf, NULL);
#endif
    }
  }
}

BUFFY *mutt_find_mailbox (const char *path)
{
  BUFFY *tmp = NULL;
  struct stat sb;
  struct stat tmp_sb;

  if (stat (path,&sb) != 0)
    return NULL;

  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
    if (stat (mutt_b2s (tmp->pathbuf), &tmp_sb) ==0 &&
	sb.st_dev == tmp_sb.st_dev && sb.st_ino == tmp_sb.st_ino)
      break;
  }
  return tmp;
}

void mutt_update_mailbox (BUFFY * b)
{
  struct stat sb;

  if (!b)
    return;

  if (stat (mutt_b2s (b->pathbuf), &sb) == 0)
    b->size = (off_t) sb.st_size;
  else
    b->size = 0;
  return;
}

static BUFFY *buffy_new (const char *path)
{
  BUFFY* buffy;
  char rp[PATH_MAX] = "";
  char *r = NULL;

  buffy = (BUFFY *) safe_calloc (1, sizeof (BUFFY));
  buffy->pathbuf = mutt_buffer_new ();
  mutt_buffer_strcpy (buffy->pathbuf, path);
  r = realpath (path, rp);
  buffy->realpath = safe_strdup (r ? rp : path);
  buffy->next = NULL;
  buffy->magic = 0;

  return buffy;
}

static void buffy_free (BUFFY **mailbox)
{
  if (!(mailbox && *mailbox))
    return;

  mutt_buffer_free (&((*mailbox)->pathbuf));
  FREE (&((*mailbox)->realpath));
  FREE (&((*mailbox)->label));
  FREE (mailbox); /* __FREE_CHECKED__ */
}

static BUFFY **find_buffy_slot (const char *path)
{
  const char *p;
  char rp[PATH_MAX];
  BUFFY **slot;

  p = realpath (path, rp);
  for (slot = &Incoming; *slot; slot = &((*slot)->next))
    if (mutt_strcmp (p ? p : path, (*slot)->realpath) == 0)
      break;

  return slot;
}

/* To avoid overwriting existing values:
 * - label should be NULL if unspecified
 * - nopoll should be -1 if unspecified
 */
void mutt_buffy_add (const char *path, const char *label, int nopoll)
{
  BUFFY **tmp;
  struct stat sb;
  int new = 0;

  if (!path || !*path)
    return;

  dprint (3, (debugfile, "mutt_buffy_add: %s\n", path));

  tmp = find_buffy_slot (path);
  if (!*tmp)
  {
    new = 1;
    *tmp = buffy_new (path);
#ifdef USE_SIDEBAR
    mutt_sb_notify_mailbox (*tmp, 1);
#endif
  }

  if (label)
    mutt_str_replace (&(*tmp)->label, label);

  if (nopoll == -1)
    nopoll = (*tmp)->nopoll;

  if (new || (nopoll != (*tmp)->nopoll))
  {
    (*tmp)->nopoll = nopoll;
#ifdef USE_INOTIFY
    if (!nopoll)
    {
      (*tmp)->magic = mx_get_magic (mutt_b2s ((*tmp)->pathbuf));
      mutt_monitor_add (*tmp);
    }
    else
      mutt_monitor_remove (*tmp);
#endif
  }

  (*tmp)->new = 0;
  (*tmp)->notified = 1;
  (*tmp)->newly_created = 0;

  /* for check_mbox_size, it is important that if the folder is new (tested by
   * reading it), the size is set to 0 so that later when we check we see
   * that it increased .  without check_mbox_size we probably don't care.
   */
  if (!nopoll &&
      option(OPTCHECKMBOXSIZE) &&
      stat (mutt_b2s ((*tmp)->pathbuf), &sb) == 0 &&
      !test_new_folder (mutt_b2s ((*tmp)->pathbuf)))
  {
    /* some systems out there don't have an off_t type */
    (*tmp)->size = (off_t) sb.st_size;
  }
  else
    (*tmp)->size = 0;
}

static void buffy_remove (BUFFY **pbuffy)
{
  BUFFY *next;

  next = (*pbuffy)->next;

#ifdef USE_SIDEBAR
  mutt_sb_notify_mailbox (*pbuffy, 0);
#endif
#ifdef USE_INOTIFY
  if (!(*pbuffy)->nopoll)
    mutt_monitor_remove (*pbuffy);
#endif
  buffy_free (pbuffy);

  *pbuffy = next;
}

void mutt_buffy_remove (const char *path)
{
  BUFFY **pbuffy;

  if (!path || !*path)
    return;

  dprint (3, (debugfile, "mutt_buffy_remove: %s\n", path));

  pbuffy = find_buffy_slot (path);
  if (*pbuffy)
    buffy_remove (pbuffy);
}

int mutt_parse_mailboxes (BUFFER *path, BUFFER *s, union pointer_long_t udata,
                          BUFFER *err)
{
  BUFFER *label = NULL;
  BUFFER *mailbox = NULL;
  int nopoll = -1, rc = -1;
  int label_set = 0, mailbox_set = 0;

  mailbox = mutt_buffer_pool_get ();
  label = mutt_buffer_pool_get ();

  while (MoreArgs (s))
  {
    do
    {
      mutt_extract_token (path, s, 0);

      if (mutt_strcmp (mutt_b2s (path), "-poll") == 0)
        nopoll = 0;
      else if (mutt_strcmp (mutt_b2s (path), "-nopoll") == 0)
        nopoll = 1;
      else if (mutt_strcmp (mutt_b2s (path), "-label") == 0)
      {
        if (!MoreArgs (s))
        {
          mutt_buffer_strcpy (err, _("too few arguments"));
          goto cleanup;
        }
        label_set = 1;
        mutt_extract_token (label, s, 0);
      }
      else if (mutt_strcmp (mutt_b2s (path), "-nolabel") == 0)
      {
        label_set = 1;
        mutt_buffer_clear (label);
      }
      else
      {
        mailbox_set = 1;
        mutt_buffer_strcpy (mailbox, mutt_b2s (path));
        mutt_buffer_expand_path (mailbox);
        break;
      }
    } while (MoreArgs (s));

    if (!mutt_buffer_len (mailbox))
    {
      if (!mailbox_set)
      {
        mutt_buffer_strcpy (err, _("too few arguments"));
        goto cleanup;
      }
    }
    else
      mutt_buffy_add (mutt_b2s (mailbox), label_set ? mutt_b2s (label) : NULL,
                      nopoll);

    mutt_buffer_clear (mailbox);
    mutt_buffer_clear (label);
    nopoll = -1;
    label_set = 0;
    mailbox_set = 0;
  }

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&mailbox);
  mutt_buffer_pool_release (&label);
  return rc;
}

int mutt_parse_unmailboxes (BUFFER *path, BUFFER *s, union pointer_long_t udata, BUFFER *err)
{
  BUFFY **pbuffy;

  while (MoreArgs (s))
  {
    mutt_extract_token (path, s, 0);

    if (mutt_strcmp(mutt_b2s (path),"*") == 0)
    {
      pbuffy = &Incoming;
      while (*pbuffy)
        buffy_remove (pbuffy);
      return 0;
    }

    mutt_buffer_expand_path (path);
    if (!mutt_buffer_len (path))
      continue;

    pbuffy = find_buffy_slot (mutt_b2s (path));
    if (*pbuffy)
      buffy_remove (pbuffy);
  }
  return 0;
}

/* Checks the specified maildir subdir (cur or new) for new mail or mail counts.
 * check_new:   if true, check for new mail.
 * check_stats: if true, count total, new, and flagged messages.
 * Returns 1 if the dir has new mail.
 */
static int buffy_maildir_check_dir (BUFFY* mailbox, const char *dir_name, int check_new,
                                    int check_stats)
{
  BUFFER *path = NULL;
  BUFFER *msgpath = NULL;
  DIR *dirp;
  struct dirent *de;
  char *p;
  int rc = 0;
  struct stat sb;

  path = mutt_buffer_pool_get ();
  msgpath = mutt_buffer_pool_get ();
  mutt_buffer_printf (path, "%s/%s", mutt_b2s (mailbox->pathbuf), dir_name);

  /* when $mail_check_recent is set, if the new/ directory hasn't been modified since
   * the user last exited the mailbox, then we know there is no recent mail.
   */
  if (check_new && option(OPTMAILCHECKRECENT))
  {
    if (stat(mutt_b2s (path), &sb) == 0 &&
        mutt_stat_timespec_compare (&sb, MUTT_STAT_MTIME, &mailbox->last_visited) < 0)
    {
      rc = 0;
      check_new = 0;
    }
  }

  if (! (check_new || check_stats))
    goto cleanup;

  if ((dirp = opendir (mutt_b2s (path))) == NULL)
  {
    mailbox->magic = 0;
    rc = 0;
    goto cleanup;
  }

  while ((de = readdir (dirp)) != NULL)
  {
    if (*de->d_name == '.')
      continue;

    p = strstr (de->d_name, ":2,");
    if (p && strchr (p + 3, 'T'))
      continue;

    if (check_stats)
    {
      mailbox->msg_count++;
      if (p && strchr (p + 3, 'F'))
        mailbox->msg_flagged++;
    }
    if (!p || !strchr (p + 3, 'S'))
    {
      if (check_stats)
        mailbox->msg_unread++;
      if (check_new)
      {
        if (option(OPTMAILCHECKRECENT))
        {
          mutt_buffer_printf (msgpath, "%s/%s", mutt_b2s (path), de->d_name);
          /* ensure this message was received since leaving this mailbox */
          if (stat(mutt_b2s (msgpath), &sb) == 0 &&
              (mutt_stat_timespec_compare (&sb, MUTT_STAT_CTIME, &mailbox->last_visited) <= 0))
            continue;
        }
        mailbox->new = 1;
        rc = 1;
        check_new = 0;
        if (!check_stats)
          break;
      }
    }
  }

  closedir (dirp);

cleanup:
  mutt_buffer_pool_release (&path);
  mutt_buffer_pool_release (&msgpath);

  return rc;
}

/* Checks new mail for a maildir mailbox.
 * check_stats: if true, also count total, new, and flagged messages.
 * Returns 1 if the mailbox has new mail.
 */
static int buffy_maildir_check (BUFFY* mailbox, int check_stats)
{
  int rc, check_new = 1;

  if (check_stats)
  {
    mailbox->msg_count   = 0;
    mailbox->msg_unread  = 0;
    mailbox->msg_flagged = 0;
  }

  rc = buffy_maildir_check_dir (mailbox, "new", check_new, check_stats);

  check_new = !rc && option (OPTMAILDIRCHECKCUR);
  if (check_new || check_stats)
    if (buffy_maildir_check_dir (mailbox, "cur", check_new, check_stats))
      rc = 1;

  return rc;
}

/* Checks new mail for an mbox mailbox
 * check_stats: if true, also count total, new, and flagged messages.
 * Returns 1 if the mailbox has new mail.
 */
static int buffy_mbox_check (BUFFY* mailbox, struct stat *sb, int check_stats)
{
  int rc = 0;
  int new_or_changed;
  CONTEXT ctx;

  if (option (OPTCHECKMBOXSIZE))
    new_or_changed = sb->st_size > mailbox->size;
  else
    new_or_changed = (mutt_stat_compare (sb, MUTT_STAT_MTIME, sb, MUTT_STAT_ATIME) > 0)
      || (mailbox->newly_created &&
          (mutt_stat_compare (sb, MUTT_STAT_CTIME, sb, MUTT_STAT_MTIME) == 0) &&
          (mutt_stat_compare (sb, MUTT_STAT_CTIME, sb, MUTT_STAT_ATIME) == 0));

  if (new_or_changed)
  {
    if (!option(OPTMAILCHECKRECENT) ||
        (mutt_stat_timespec_compare (sb, MUTT_STAT_MTIME, &mailbox->last_visited) > 0))
    {
      rc = 1;
      mailbox->new = 1;
    }
  }
  else if (option(OPTCHECKMBOXSIZE))
  {
    /* some other program has deleted mail from the folder */
    mailbox->size = (off_t) sb->st_size;
  }

  if (mailbox->newly_created &&
      (sb->st_ctime != sb->st_mtime || sb->st_ctime != sb->st_atime))
    mailbox->newly_created = 0;

  if (check_stats &&
      (mutt_stat_timespec_compare (sb, MUTT_STAT_MTIME, &mailbox->stats_last_checked) > 0))
  {
    if (mx_open_mailbox (mutt_b2s (mailbox->pathbuf),
                         MUTT_READONLY | MUTT_QUIET | MUTT_NOSORT | MUTT_PEEK,
                         &ctx) != NULL)
    {
      mailbox->msg_count       = ctx.msgcount;
      mailbox->msg_unread      = ctx.unread;
      mailbox->msg_flagged     = ctx.flagged;
      mailbox->stats_last_checked = ctx.mtime;
      mx_close_mailbox (&ctx, 0);
    }
  }

  return rc;
}

/* Check all Incoming for new mail and total/new/flagged messages
 * The force argument may be any combination of the following values:
 *   MUTT_BUFFY_CHECK_FORCE        ignore BuffyTimeout and check for new mail
 *   MUTT_BUFFY_CHECK_FORCE_STATS  ignore BuffyTimeout and calculate statistics
 */
int mutt_buffy_check (int force)
{
  BUFFY *tmp;
  struct stat sb;
  struct stat contex_sb;
  time_t t;
  int check_stats = 0;
#ifdef USE_SIDEBAR
  short orig_new;
  int orig_count, orig_unread, orig_flagged;
#endif

  sb.st_size=0;
  contex_sb.st_dev=0;
  contex_sb.st_ino=0;

#ifdef USE_IMAP
  /* update postponed count as well, on force */
  if (force & MUTT_BUFFY_CHECK_FORCE)
    mutt_update_num_postponed ();
#endif

  /* fastest return if there are no mailboxes */
  if (!Incoming)
    return 0;
  t = time (NULL);
  if (!force && (t - BuffyTime < BuffyTimeout))
    return BuffyCount;

  if ((force & MUTT_BUFFY_CHECK_FORCE_STATS) ||
      (option (OPTMAILCHECKSTATS) &&
       (t - BuffyStatsTime >= BuffyCheckStatsInterval)))
  {
    check_stats = 1;
    BuffyStatsTime = t;
  }

  BuffyTime = t;
  BuffyCount = 0;
  BuffyNotify = 0;

#ifdef USE_IMAP
  BuffyCount += imap_buffy_check (force, check_stats);
#endif

  /* check device ID and serial number instead of comparing paths */
  if (!Context || Context->magic == MUTT_IMAP || Context->magic == MUTT_POP
      || stat (Context->path, &contex_sb) != 0)
  {
    contex_sb.st_dev=0;
    contex_sb.st_ino=0;
  }

  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
    if (tmp->nopoll)
      continue;

#ifdef USE_SIDEBAR
    orig_new = tmp->new;
    orig_count = tmp->msg_count;
    orig_unread = tmp->msg_unread;
    orig_flagged = tmp->msg_flagged;
#endif

    if (tmp->magic != MUTT_IMAP)
    {
      tmp->new = 0;
#ifdef USE_POP
      if (mx_is_pop (mutt_b2s (tmp->pathbuf)))
	tmp->magic = MUTT_POP;
      else
#endif
        if (stat (mutt_b2s (tmp->pathbuf), &sb) != 0 ||
            (S_ISREG(sb.st_mode) && sb.st_size == 0) ||
            (!tmp->magic &&
             (tmp->magic = mx_get_magic (mutt_b2s (tmp->pathbuf))) <= 0))
        {
          /* if the mailbox still doesn't exist, set the newly created flag to
           * be ready for when it does. */
          tmp->newly_created = 1;
          tmp->magic = 0;
          tmp->size = 0;
          continue;
        }
    }

    /* check to see if the folder is the currently selected folder
     * before polling */
    if (!Context || !Context->path ||
	(( tmp->magic == MUTT_IMAP || tmp->magic == MUTT_POP )
            ? mutt_strcmp (mutt_b2s (tmp->pathbuf), Context->path) :
	      (sb.st_dev != contex_sb.st_dev || sb.st_ino != contex_sb.st_ino)))
    {
      switch (tmp->magic)
      {
        case MUTT_MBOX:
        case MUTT_MMDF:
          if (buffy_mbox_check (tmp, &sb, check_stats) > 0)
            BuffyCount++;
          break;

        case MUTT_MAILDIR:
          if (buffy_maildir_check (tmp, check_stats) > 0)
            BuffyCount++;
          break;

        case MUTT_MH:
          if (mh_buffy (tmp, check_stats) > 0)
            BuffyCount++;
          break;
      }
    }
    else if (option(OPTCHECKMBOXSIZE) && Context && Context->path)
      tmp->size = (off_t) sb.st_size;	/* update the size of current folder */

#ifdef USE_SIDEBAR
    if ((orig_new != tmp->new) ||
        (orig_count != tmp->msg_count) ||
        (orig_unread != tmp->msg_unread) ||
        (orig_flagged != tmp->msg_flagged))
      mutt_set_current_menu_redraw (REDRAW_SIDEBAR);
#endif

    if (!tmp->new)
      tmp->notified = 0;
    else if (!tmp->notified)
      BuffyNotify++;
  }

  BuffyDoneTime = BuffyTime;
  return (BuffyCount);
}

int mutt_buffy_list (void)
{
  BUFFY *tmp;
  BUFFER *path = NULL;
  char buffylist[2*STRING];
  size_t pos = 0;
  int first = 1;

  int have_unnotified = BuffyNotify;

  path = mutt_buffer_pool_get ();

  buffylist[0] = 0;
  pos += strlen (strncat (buffylist, _("New mail in "), sizeof (buffylist) - 1 - pos)); /* __STRNCAT_CHECKED__ */
  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
    /* Is there new mail in this mailbox? */
    if (!tmp->new || (have_unnotified && tmp->notified))
      continue;

    mutt_buffer_strcpy (path, mutt_b2s (tmp->pathbuf));
    mutt_buffer_pretty_mailbox (path);

    if (!first && (MuttMessageWindow->cols >= 7) &&
        (pos + mutt_buffer_len (path) >= (size_t)MuttMessageWindow->cols - 7))
      break;

    if (!first)
      pos += strlen (strncat(buffylist + pos, ", ", sizeof(buffylist)-1-pos)); /* __STRNCAT_CHECKED__ */

    /* Prepend an asterisk to mailboxes not already notified */
    if (!tmp->notified)
    {
      /* pos += strlen (strncat(buffylist + pos, "*", sizeof(buffylist)-1-pos));  __STRNCAT_CHECKED__ */
      tmp->notified = 1;
      BuffyNotify--;
    }
    pos += strlen (strncat(buffylist + pos, mutt_b2s (path), sizeof(buffylist)-1-pos)); /* __STRNCAT_CHECKED__ */
    first = 0;
  }
  if (!first && tmp)
  {
    strncat (buffylist + pos, ", ...", sizeof (buffylist) - 1 - pos); /* __STRNCAT_CHECKED__ */
  }

  mutt_buffer_pool_release (&path);

  if (!first)
  {
    mutt_message ("%s", buffylist);
    return (1);
  }
  else
  {
    /* there were no mailboxes needing to be notified, so clean up since
     * BuffyNotify has somehow gotten out of sync
     */
    BuffyNotify = 0;
    return (0);
  }
}

void mutt_buffy_setnotified (const char *path)
{
  BUFFY *buffy;

  buffy = buffy_get(path);
  if (!buffy)
    return;

  buffy->notified = 1;
#if HAVE_CLOCK_GETTIME
  clock_gettime (CLOCK_REALTIME, &buffy->last_visited);
#else
  buffy->last_visited.tv_nsec = 0;
  time(&buffy->last_visited.tv_sec);
#endif
}

int mutt_buffy_notify (void)
{
  if (mutt_buffy_check (0) && BuffyNotify)
  {
    return (mutt_buffy_list ());
  }
  return (0);
}

void mutt_buffy (char *s, size_t slen)
{
  BUFFER *s_buf;

  s_buf = mutt_buffer_pool_get ();

  mutt_buffer_addstr (s_buf, NONULL (s));
  mutt_buffer_buffy (s_buf);
  strfcpy (s, mutt_b2s (s_buf), slen);

  mutt_buffer_pool_release (&s_buf);
}

/*
 * mutt_buffy() -- incoming folders completion routine
 *
 * given a folder name, this routine gives the next incoming folder with new
 * mail.
 */
void mutt_buffer_buffy (BUFFER *s)
{
  BUFFY *tmp = Incoming;
  int pass, found = 0;

  mutt_buffer_expand_path (s);

  if (mutt_buffy_check (0))
  {
    for (pass = 0; pass < 2; pass++)
      for (tmp = Incoming; tmp; tmp = tmp->next)
      {
	mutt_buffer_expand_path (tmp->pathbuf);
	if ((found || pass) && tmp->new)
	{
	  mutt_buffer_strcpy (s, mutt_b2s (tmp->pathbuf));
	  mutt_buffer_pretty_mailbox (s);
	  return;
	}
	if (mutt_strcmp (mutt_b2s (s), mutt_b2s (tmp->pathbuf)) == 0)
	  found = 1;
      }

    mutt_buffy_check (MUTT_BUFFY_CHECK_FORCE); /* buffy was wrong - resync
						  things */
  }

  /* no folders with new mail */
  mutt_buffer_clear (s);
}

/* fetch buffy object for given path, if present */
static BUFFY* buffy_get (const char *path)
{
  BUFFY *cur;
  BUFFER *epath;

  if (!path)
    return NULL;

  epath = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (epath, NONULL (path));
  mutt_buffer_expand_path (epath);

  for (cur = Incoming; cur; cur = cur->next)
  {
    /* must be done late because e.g. IMAP delimiter may change */
    mutt_buffer_expand_path (cur->pathbuf);
    if (!mutt_strcmp (mutt_b2s (cur->pathbuf), mutt_b2s (epath)))
    {
      mutt_buffer_pool_release (&epath);
      return cur;
    }
  }

  mutt_buffer_pool_release (&epath);
  return NULL;
}
