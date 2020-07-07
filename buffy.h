/*
 * Copyright (C) 1996-2000,2010,2013 Michael R. Elkins <me@mutt.org>
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

#ifndef _BUFFY_H
#define _BUFFY_H

typedef struct buffy_t
{
  BUFFER *pathbuf;
  const char *realpath; /* used for duplicate detection, context comparison,
                           and the sidebar */
  char *label;    /* an optional label for the mailbox */

  off_t size;
  struct buffy_t *next;
  short new;			/* mailbox has new mail */

  /* These next three are only set when OPTMAILCHECKSTATS is set */
  int msg_count;		/* total number of messages */
  int msg_unread;		/* number of unread messages */
  int msg_flagged;		/* number of flagged messages */

  short nopoll;                 /* if set, don't poll for new mail */
  short notified;		/* user has been notified */
  short magic;			/* mailbox type */
  short newly_created;		/* mbox or mmdf just popped into existence */
  struct timespec last_visited;		/* time of last exit from this mailbox */
  struct timespec stats_last_checked;	/* mtime of mailbox the last time stats where checked. */
}
BUFFY;

WHERE BUFFY *Incoming;
WHERE short BuffyTimeout INITVAL (3);
WHERE short BuffyCheckStatsInterval INITVAL (60);

extern time_t BuffyDoneTime;	/* last time we knew for sure how much mail there was */

void mutt_buffy_add (const char *path, const char *label, int nopoll);
void mutt_buffy_remove (const char *path);

void mutt_buffer_buffy (BUFFER *);
void mutt_buffy (char *, size_t);

int  mutt_buffy_list (void);
int mutt_buffy_check (int);
int mutt_buffy_notify (void);

BUFFY *mutt_find_mailbox (const char *path);
void mutt_update_mailbox (BUFFY * b);

/* fixes up atime + mtime after mbox/mmdf mailbox was modified
   according to stat() info taken before a modification */
void mutt_buffy_cleanup (const char *buf, struct stat *st);

/* mark mailbox just left as already notified */
void mutt_buffy_setnotified (const char *path);

int mh_buffy (BUFFY *, int);

/* force flags passed to mutt_buffy_check() */
#define MUTT_BUFFY_CHECK_FORCE       1
#define MUTT_BUFFY_CHECK_FORCE_STATS (1<<1)

#endif /* _BUFFY_H */
