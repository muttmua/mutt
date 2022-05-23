/*
 * Copyright (C) 1996-2000,2007,2010,2013 Michael R. Elkins <me@mutt.org>
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
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "attach.h"
#include "buffy.h"
#include "mapping.h"
#include "sort.h"
#include "mailbox.h"
#include "browser.h"
#ifdef USE_IMAP
#include "imap.h"
#endif

#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <locale.h>

static const struct mapping_t FolderHelp[] = {
  { N_("Exit"),  OP_EXIT },
  { N_("Chdir"), OP_CHANGE_DIRECTORY },
  { N_("Mask"),  OP_ENTER_MASK },
  { N_("Help"),  OP_HELP },
  { NULL,	 0 }
};

typedef struct folder_t
{
  struct folder_file *ff;
  int num;
} FOLDER;

static BUFFER *LastDir = NULL;
static BUFFER *LastDirBackup = NULL;

void mutt_browser_cleanup (void)
{
  mutt_buffer_free (&LastDir);
  mutt_buffer_free (&LastDirBackup);
}

/* Frees up the memory allocated for the local-global variables.  */
static void destroy_state (struct browser_state *state)
{
  int c;

  for (c = 0; c < state->entrylen; c++)
  {
    FREE (&((state->entry)[c].display_name));
    FREE (&((state->entry)[c].full_path));
  }
#ifdef USE_IMAP
  FREE (&state->folder);
#endif
  FREE (&state->entry);
}

/* This is set by browser_sort() */
static int sort_reverse_flag = 0;

static int browser_compare_order (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_numeric_cmp (pa->number, pb->number);

  return (sort_reverse_flag ? -r : r);
}

static int browser_compare_subject (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_strcoll (pa->display_name, pb->display_name);

  return (sort_reverse_flag ? -r : r);
}

static int browser_compare_date (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_numeric_cmp (pa->mtime, pb->mtime);

  return (sort_reverse_flag ? -r : r);
}

static int browser_compare_size (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_numeric_cmp (pa->size, pb->size);

  return (sort_reverse_flag ? -r : r);
}

static int browser_compare_count (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_numeric_cmp (pa->msg_count, pb->msg_count);

  return (sort_reverse_flag ? -r : r);
}

static int browser_compare_unread (const void *a, const void *b)
{
  struct folder_file *pa = (struct folder_file *) a;
  struct folder_file *pb = (struct folder_file *) b;

  int r = mutt_numeric_cmp (pa->msg_unread, pb->msg_unread);

  return (sort_reverse_flag ? -r : r);
}

static void browser_sort (struct browser_state *state)
{
  int (*f) (const void *, const void *);
  short sort_variable;
  unsigned int first_sort_index = 0;

  sort_variable = state->buffy ? BrowserSortMailboxes : BrowserSort;

  switch (sort_variable & SORT_MASK)
  {
    case SORT_ORDER:
      f = browser_compare_order;
      break;
    case SORT_DATE:
      f = browser_compare_date;
      break;
    case SORT_SIZE:
      f = browser_compare_size;
      break;
    case SORT_COUNT:
      f = browser_compare_count;
      break;
    case SORT_UNREAD:
      f = browser_compare_unread;
      break;
    case SORT_SUBJECT:
    default:
      f = browser_compare_subject;
      break;
  }

  sort_reverse_flag = (sort_variable & SORT_REVERSE) ? 1 : 0;

  /* Keep the ".." entry at the top in file mode. */
  if (!state->buffy)
  {
    unsigned int i;
    struct folder_file tmp;

    for (i = 0; i < state->entrylen; i++)
    {
      if ((mutt_strcmp (state->entry[i].display_name, "..") == 0) ||
          (mutt_strcmp (state->entry[i].display_name, "../") == 0))
      {
        first_sort_index = 1;
        if (i != 0)
        {
          tmp = state->entry[0];
          state->entry[0] = state->entry[i];
          state->entry[i] = tmp;
        }
        break;
      }
    }
  }

  if (state->entrylen > first_sort_index)
    qsort (state->entry + first_sort_index, state->entrylen - first_sort_index,
           sizeof (struct folder_file), f);
}

/* Returns 1 if a resort is required. */
static int select_sort (struct browser_state *state, int reverse)
{
  int resort = 1;
  int new_sort;

  switch (mutt_multi_choice ((reverse) ?
           _("Reverse sort by (d)ate, (a)lpha, si(z)e, (c)ount, (u)nread, or do(n)'t sort? ") :
           _("Sort by (d)ate, (a)lpha, si(z)e, (c)ount, (u)nread, or do(n)'t sort? "),
           _("dazcun")))
  {
    case 1: /* (d)ate */
      new_sort = SORT_DATE;
      break;

    case 2: /* (a)lpha */
      new_sort = SORT_SUBJECT;
      break;

    case 3: /* si(z)e */
      new_sort = SORT_SIZE;
      break;

    case 4: /* (c)ount */
      new_sort = SORT_COUNT;
      break;

    case 5: /* (u)nread */
      new_sort = SORT_UNREAD;
      break;

    case 6: /* do(n)'t sort */
      new_sort = SORT_ORDER;
      break;

    case -1: /* abort */
    default:
      resort = 0;
      goto bail;
      break;
  }

  new_sort |= reverse ? SORT_REVERSE : 0;
  if (state->buffy)
    BrowserSortMailboxes = new_sort;
  else
    BrowserSort = new_sort;

bail:
  return resort;
}

static int link_is_dir (const char *full_path)
{
  struct stat st;
  int retval = 0;

  if (stat (full_path, &st) == 0)
    retval = S_ISDIR (st.st_mode);

  return retval;
}

static const char *
folder_format_str (char *dest, size_t destlen, size_t col, int cols, char op, const char *src,
		   const char *fmt, const char *ifstring, const char *elsestring,
		   void *data, format_flag flags)
{
  char fn[SHORT_STRING], tmp[SHORT_STRING], permission[11];
  char date[SHORT_STRING], *t_fmt;
  time_t tnow;
  FOLDER *folder = (FOLDER *) data;
  struct passwd *pw;
  struct group *gr;
  int optional = (flags & MUTT_FORMAT_OPTIONAL);

  switch (op)
  {
    case 'C':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, folder->num + 1);
      break;

    case 'd':
    case 'D':
      if (folder->ff->local)
      {
	int do_locales = TRUE;

	if (op == 'D')
        {
	  t_fmt = NONULL(DateFmt);
	  if (*t_fmt == '!')
          {
	    ++t_fmt;
	    do_locales = FALSE;
	  }
	}
        else
        {
	  tnow = time (NULL);
	  t_fmt = tnow - folder->ff->mtime < 31536000 ? "%b %d %H:%M" : "%b %d  %Y";
	}

        if (!do_locales)
          setlocale (LC_TIME, "C");
        strftime (date, sizeof (date), t_fmt, localtime (&folder->ff->mtime));
        if (!do_locales)
          setlocale (LC_TIME, "");

	mutt_format_s (dest, destlen, fmt, date);
      }
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    case 'f':
    {
      char *s = NONULL (folder->ff->display_name);

      snprintf (fn, sizeof (fn), "%s%s", s,
		folder->ff->local ?
                (S_ISLNK (folder->ff->mode) ?
                 "@" :
                 (S_ISDIR (folder->ff->mode) ?
                  "/" :
                  ((folder->ff->mode & S_IXUSR) != 0 ?
                   "*" :
                   ""))) :
                "");

      mutt_format_s (dest, destlen, fmt, fn);
      break;
    }
    case 'F':
      if (folder->ff->local)
      {
	snprintf (permission, sizeof (permission), "%c%c%c%c%c%c%c%c%c%c",
		  S_ISDIR(folder->ff->mode) ? 'd' : (S_ISLNK(folder->ff->mode) ? 'l' : '-'),
		  (folder->ff->mode & S_IRUSR) != 0 ? 'r': '-',
		  (folder->ff->mode & S_IWUSR) != 0 ? 'w' : '-',
		  (folder->ff->mode & S_ISUID) != 0 ? 's' : (folder->ff->mode & S_IXUSR) != 0 ? 'x': '-',
		  (folder->ff->mode & S_IRGRP) != 0 ? 'r' : '-',
		  (folder->ff->mode & S_IWGRP) != 0 ? 'w' : '-',
		  (folder->ff->mode & S_ISGID) != 0 ? 's' : (folder->ff->mode & S_IXGRP) != 0 ? 'x': '-',
		  (folder->ff->mode & S_IROTH) != 0 ? 'r' : '-',
		  (folder->ff->mode & S_IWOTH) != 0 ? 'w' : '-',
		  (folder->ff->mode & S_ISVTX) != 0 ? 't' : (folder->ff->mode & S_IXOTH) != 0 ? 'x': '-');
	mutt_format_s (dest, destlen, fmt, permission);
      }
#ifdef USE_IMAP
      else if (folder->ff->imap)
      {
	/* mark folders with subfolders AND mail */
	snprintf (permission, sizeof (permission), "IMAP %c",
		  (folder->ff->inferiors && folder->ff->selectable) ? '+' : ' ');
	mutt_format_s (dest, destlen, fmt, permission);
      }
#endif
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    case 'g':
      if (folder->ff->local)
      {
	if ((gr = getgrgid (folder->ff->gid)))
	  mutt_format_s (dest, destlen, fmt, gr->gr_name);
	else
	{
	  snprintf (tmp, sizeof (tmp), "%%%sld", fmt);
	  snprintf (dest, destlen, tmp, folder->ff->gid);
	}
      }
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    case 'l':
      if (folder->ff->local)
      {
	snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
	snprintf (dest, destlen, tmp, folder->ff->nlink);
      }
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    case 'm':
      if (!optional)
      {
        if (folder->ff->has_buffy)
        {
          snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
          snprintf (dest, destlen, tmp, folder->ff->msg_count);
        }
        else
          mutt_format_s (dest, destlen, fmt, "");
      }
      else if (!folder->ff->msg_count)
        optional = 0;
      break;

    case 'N':
      snprintf (tmp, sizeof (tmp), "%%%sc", fmt);
      snprintf (dest, destlen, tmp, folder->ff->new ? 'N' : ' ');
      break;

    case 'n':
      if (!optional)
      {
        if (folder->ff->has_buffy)
        {
          snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
          snprintf (dest, destlen, tmp, folder->ff->msg_unread);
        }
        else
          mutt_format_s (dest, destlen, fmt, "");
      }
      else if (!folder->ff->msg_unread)
        optional = 0;
      break;

    case 's':
      if (folder->ff->local)
      {
	mutt_pretty_size(fn, sizeof(fn), folder->ff->size);
	snprintf (tmp, sizeof (tmp), "%%%ss", fmt);
	snprintf (dest, destlen, tmp, fn);
      }
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    case 't':
      snprintf (tmp, sizeof (tmp), "%%%sc", fmt);
      snprintf (dest, destlen, tmp, folder->ff->tagged ? '*' : ' ');
      break;

    case 'u':
      if (folder->ff->local)
      {
	if ((pw = getpwuid (folder->ff->uid)))
	  mutt_format_s (dest, destlen, fmt, pw->pw_name);
	else
	{
	  snprintf (tmp, sizeof (tmp), "%%%sld", fmt);
	  snprintf (dest, destlen, tmp, folder->ff->uid);
	}
      }
      else
	mutt_format_s (dest, destlen, fmt, "");
      break;

    default:
      snprintf (tmp, sizeof (tmp), "%%%sc", fmt);
      snprintf (dest, destlen, tmp, op);
      break;
  }

  if (optional)
    mutt_FormatString (dest, destlen, col, cols, ifstring, folder_format_str, data, 0);
  else if (flags & MUTT_FORMAT_OPTIONAL)
    mutt_FormatString (dest, destlen, col, cols, elsestring, folder_format_str, data, 0);

  return (src);
}

static void add_folder (MUTTMENU *m, struct browser_state *state,
			const char *display_name, const char *full_path,
                        const struct stat *s, BUFFY *b)
{
  if (state->entrylen == state->entrymax)
  {
    /* need to allocate more space */
    safe_realloc (&state->entry,
		  sizeof (struct folder_file) * (state->entrymax += 256));
    memset (&state->entry[state->entrylen], 0,
	    sizeof (struct folder_file) * 256);
    if (m)
      m->data = state->entry;
  }

  if (s != NULL)
  {
    (state->entry)[state->entrylen].mode = s->st_mode;
    (state->entry)[state->entrylen].mtime = s->st_mtime;
    (state->entry)[state->entrylen].size = s->st_size;
    (state->entry)[state->entrylen].gid = s->st_gid;
    (state->entry)[state->entrylen].uid = s->st_uid;
    (state->entry)[state->entrylen].nlink = s->st_nlink;

    (state->entry)[state->entrylen].local = 1;
  }

  if (b)
  {
    (state->entry)[state->entrylen].has_buffy = 1;
    (state->entry)[state->entrylen].new = b->new;
    (state->entry)[state->entrylen].msg_count = b->msg_count;
    (state->entry)[state->entrylen].msg_unread = b->msg_unread;
  }

  (state->entry)[state->entrylen].display_name = safe_strdup (display_name);
  (state->entry)[state->entrylen].full_path = safe_strdup (full_path);

  (state->entry)[state->entrylen].number = state->entrylen;

#ifdef USE_IMAP
  (state->entry)[state->entrylen].imap = 0;
#endif
  (state->entrylen)++;
}

static void init_state (struct browser_state *state, MUTTMENU *menu)
{
  state->entrylen = 0;
  state->entrymax = 256;
  state->entry = (struct folder_file *) safe_calloc (state->entrymax, sizeof (struct folder_file));
#ifdef USE_IMAP
  state->imap_browse = 0;
#endif
  if (menu)
    menu->data = state->entry;
}

static int examine_directory (MUTTMENU *menu, struct browser_state *state,
			      const char *d, const char *prefix)
{
  struct stat s;
  DIR *dp;
  struct dirent *de;
  BUFFER *full_path = NULL;
  BUFFY *tmp;

  while (stat (d, &s) == -1)
  {
    if (errno == ENOENT)
    {
      /* The last used directory is deleted, try to use the parent dir. */
      char *c = strrchr (d, '/');

      if (c && (c > d))
      {
	*c = 0;
	continue;
      }
    }
    mutt_perror (d);
    return (-1);
  }

  if (!S_ISDIR (s.st_mode))
  {
    mutt_error (_("%s is not a directory."), d);
    return (-1);
  }

  mutt_buffy_check (0);

  if ((dp = opendir (d)) == NULL)
  {
    mutt_perror (d);
    return (-1);
  }

  full_path = mutt_buffer_pool_get ();
  init_state (state, menu);

  while ((de = readdir (dp)) != NULL)
  {
    if (mutt_strcmp (de->d_name, ".") == 0)
      continue;    /* we don't need . */

    if (prefix && *prefix && mutt_strncmp (prefix, de->d_name, mutt_strlen (prefix)) != 0)
      continue;
    if (!((regexec (Mask.rx, de->d_name, 0, NULL, 0) == 0) ^ Mask.not))
      continue;

    mutt_buffer_concat_path (full_path, d, de->d_name);
    if (lstat (mutt_b2s (full_path), &s) == -1)
      continue;

    /* No size for directories or symlinks */
    if (S_ISDIR (s.st_mode) || S_ISLNK (s.st_mode))
      s.st_size = 0;
    else if (! S_ISREG (s.st_mode))
      continue;

    tmp = Incoming;
    while (tmp && mutt_strcmp (mutt_b2s (full_path), mutt_b2s (tmp->pathbuf)))
      tmp = tmp->next;
    if (tmp && Context && !tmp->nopoll &&
        !mutt_strcmp (tmp->realpath, Context->realpath))
    {
      tmp->msg_count = Context->msgcount;
      tmp->msg_unread = Context->unread;
    }
    add_folder (menu, state, de->d_name, mutt_b2s (full_path), &s, tmp);
  }
  closedir (dp);
  browser_sort (state);

  mutt_buffer_pool_release (&full_path);
  return 0;
}

static int examine_mailboxes (MUTTMENU *menu, struct browser_state *state)
{
  struct stat s;
  BUFFY *tmp = Incoming;
  BUFFER *mailbox = NULL;
  BUFFER *md = NULL;

  if (!Incoming)
    return (-1);
  mutt_buffy_check (0);

  mailbox = mutt_buffer_pool_get ();
  md = mutt_buffer_pool_get ();
  init_state (state, menu);

  do
  {
    if (Context && !tmp->nopoll &&
        !mutt_strcmp (tmp->realpath, Context->realpath))
    {
      tmp->msg_count = Context->msgcount;
      tmp->msg_unread = Context->unread;
    }

    if (tmp->label)
      mutt_buffer_strcpy (mailbox, tmp->label);
    else
    {
      if (option (OPTBROWSERABBRMAILBOXES))
      {
        mutt_buffer_strcpy (mailbox, mutt_b2s (tmp->pathbuf));
        mutt_buffer_pretty_mailbox (mailbox);
      }
      else
        mutt_buffer_remove_path_password (mailbox, mutt_b2s (tmp->pathbuf));
    }

#ifdef USE_IMAP
    if (mx_is_imap (mutt_b2s (tmp->pathbuf)))
    {
      add_folder (menu, state, mutt_b2s (mailbox), mutt_b2s (tmp->pathbuf), NULL, tmp);
      continue;
    }
#endif
#ifdef USE_POP
    if (mx_is_pop (mutt_b2s (tmp->pathbuf)))
    {
      add_folder (menu, state, mutt_b2s (mailbox), mutt_b2s (tmp->pathbuf), NULL, tmp);
      continue;
    }
#endif
    if (lstat (mutt_b2s (tmp->pathbuf), &s) == -1)
      continue;

    if ((! S_ISREG (s.st_mode)) && (! S_ISDIR (s.st_mode)) &&
	(! S_ISLNK (s.st_mode)))
      continue;

    if (mx_is_maildir (mutt_b2s (tmp->pathbuf)))
    {
      struct stat st2;

      mutt_buffer_printf (md, "%s/new", mutt_b2s (tmp->pathbuf));
      if (stat (mutt_b2s (md), &s) < 0)
	s.st_mtime = 0;
      mutt_buffer_printf (md, "%s/cur", mutt_b2s (tmp->pathbuf));
      if (stat (mutt_b2s (md), &st2) < 0)
	st2.st_mtime = 0;
      if (st2.st_mtime > s.st_mtime)
	s.st_mtime = st2.st_mtime;
    }

    add_folder (menu, state, mutt_b2s (mailbox), mutt_b2s (tmp->pathbuf), &s, tmp);
  }
  while ((tmp = tmp->next));
  browser_sort (state);

  mutt_buffer_pool_release (&mailbox);
  mutt_buffer_pool_release (&md);
  return 0;
}

static int select_file_search (MUTTMENU *menu, regex_t *re, int n)
{
  return (regexec (re, ((struct folder_file *) menu->data)[n].display_name, 0, NULL, 0));
}

static void folder_entry (char *s, size_t slen, MUTTMENU *menu, int num)
{
  FOLDER folder;

  folder.ff = &((struct folder_file *) menu->data)[num];
  folder.num = num;

  mutt_FormatString (s, slen, 0, MuttIndexWindow->cols, NONULL(FolderFormat), folder_format_str,
                     &folder, MUTT_FORMAT_ARROWCURSOR);
}

static void set_sticky_cursor (struct browser_state *state, MUTTMENU *menu, const char *defaultsel)
{
  int i;

  if (option (OPTBROWSERSTICKYCURSOR) && defaultsel && *defaultsel)
  {
    for (i = 0; i < menu->max; i++)
    {
      if (!mutt_strcmp (defaultsel, state->entry[i].full_path))
      {
        menu->current = i;
        break;
      }
    }
  }
}

static void init_menu (struct browser_state *state, MUTTMENU *menu, char *title,
		       size_t titlelen, const char *defaultsel)
{
  BUFFER *path = NULL;

  path = mutt_buffer_pool_get ();

  menu->max = state->entrylen;

  if (menu->current >= menu->max)
    menu->current = menu->max - 1;
  if (menu->current < 0)
    menu->current = 0;
  if (menu->top > menu->current)
    menu->top = 0;

  menu->tagged = 0;

  if (state->buffy)
    snprintf (title, titlelen, _("Mailboxes [%d]"), mutt_buffy_check (0));
  else
  {
    mutt_buffer_strcpy (path, mutt_b2s (LastDir));
    mutt_buffer_pretty_mailbox (path);
#ifdef USE_IMAP
    if (state->imap_browse && option (OPTIMAPLSUB))
      snprintf (title, titlelen, _("Subscribed [%s], File mask: %s"),
                mutt_b2s (path), NONULL (Mask.pattern));
    else
#endif
      snprintf (title, titlelen, _("Directory [%s], File mask: %s"),
                mutt_b2s (path), NONULL(Mask.pattern));
  }
  menu->redraw = REDRAW_FULL;

  set_sticky_cursor (state, menu, defaultsel);

  mutt_buffer_pool_release (&path);
}

static int file_tag (MUTTMENU *menu, int n, int m)
{
  struct folder_file *ff = &(((struct folder_file *)menu->data)[n]);
  int ot;
  if (S_ISDIR (ff->mode) ||
      (S_ISLNK (ff->mode) && link_is_dir (ff->full_path)))
  {
    mutt_error _("Can't attach a directory!");
    return 0;
  }

  ot = ff->tagged;
  ff->tagged = (m >= 0 ? m : !ff->tagged);

  return ff->tagged - ot;
}

void _mutt_select_file (char *f, size_t flen, int flags, char ***files, int *numfiles)
{
  BUFFER *f_buf = NULL;

  f_buf = mutt_buffer_pool_get ();

  mutt_buffer_strcpy (f_buf, NONULL (f));
  _mutt_buffer_select_file (f_buf, flags, files, numfiles);
  strfcpy (f, mutt_b2s (f_buf), flen);

  mutt_buffer_pool_release (&f_buf);
}

/* If flags & MUTT_SELECT_MULTI is set, numfiles and files will contain
 * the (one or more) selected files.
 *
 * If MUTT_SELECT_MULTI is not set, then the result, if any, will be in f
 */
void _mutt_buffer_select_file (BUFFER *f, int flags, char ***files, int *numfiles)
{
  BUFFER *buf = NULL;
  BUFFER *prefix = NULL;
  BUFFER *tmp = NULL;
  BUFFER *OldLastDir = NULL;
  BUFFER *defaultsel = NULL;
  char helpstr[LONG_STRING];
  char title[STRING];
  struct browser_state state;
  MUTTMENU *menu = NULL;
  struct stat st;
  int op, killPrefix = 0;
  int i, j;
  int multiple = (flags & MUTT_SEL_MULTI)  ? 1 : 0;
  int folder   = (flags & MUTT_SEL_FOLDER) ? 1 : 0;
  int buffy    = (flags & MUTT_SEL_BUFFY)  ? 1 : 0;

  buffy = buffy && folder;

  buf        = mutt_buffer_pool_get ();
  prefix     = mutt_buffer_pool_get ();
  tmp        = mutt_buffer_pool_get ();
  OldLastDir = mutt_buffer_pool_get ();
  defaultsel  = mutt_buffer_pool_get ();

  memset (&state, 0, sizeof (struct browser_state));

  state.buffy = buffy;

  if (!LastDir)
  {
    LastDir = mutt_buffer_new ();
    mutt_buffer_increase_size (LastDir, _POSIX_PATH_MAX);
    LastDirBackup = mutt_buffer_new ();
    mutt_buffer_increase_size (LastDirBackup, _POSIX_PATH_MAX);
  }

  if (!folder)
    mutt_buffer_strcpy (LastDirBackup, mutt_b2s (LastDir));

  if (*(mutt_b2s (f)))
  {
    /* Note we use _norel because:
     * 1) The code below already handles relative path expansion.
     * 2) Browser completion listing handles 'dir/' differently from
     *    'dir'.  The former will list the content of the directory.
     *    The latter will list current directory completions with
     *    prefix 'dir'.
     */
    mutt_buffer_expand_path_norel (f);
#ifdef USE_IMAP
    if (mx_is_imap (mutt_b2s (f)))
    {
      init_state (&state, NULL);
      state.imap_browse = 1;
      if (!imap_browse (mutt_b2s (f), &state))
        mutt_buffer_strcpy (LastDir, state.folder);
    }
    else
    {
#endif
      for (i = mutt_buffer_len (f) - 1;
           i > 0 && (mutt_b2s (f))[i] != '/' ;
           i--);
      if (i > 0)
      {
        if ((mutt_b2s (f))[0] == '/')
          mutt_buffer_strcpy_n (LastDir, mutt_b2s (f), i);
        else
        {
          mutt_getcwd (LastDir);
          mutt_buffer_addch (LastDir, '/');
          mutt_buffer_addstr_n (LastDir, mutt_b2s (f), i);
        }
      }
      else
      {
        if ((mutt_b2s (f))[0] == '/')
          mutt_buffer_strcpy (LastDir, "/");
        else
          mutt_getcwd (LastDir);
      }

      if (i <= 0 && (mutt_b2s (f))[0] != '/')
        mutt_buffer_strcpy (prefix, mutt_b2s (f));
      else
        mutt_buffer_strcpy (prefix, mutt_b2s (f) + i + 1);
      killPrefix = 1;
#ifdef USE_IMAP
    }
#endif
  }
  else
  {
    if (!folder)
      mutt_getcwd (LastDir);
    else if (!*(mutt_b2s (LastDir)))
      mutt_buffer_strcpy (LastDir, NONULL(Maildir));

    if (Context)
      mutt_buffer_strcpy (defaultsel, NONULL (Context->path));

#ifdef USE_IMAP
    if (!state.buffy && mx_is_imap (mutt_b2s (LastDir)))
    {
      init_state (&state, NULL);
      state.imap_browse = 1;
      imap_browse (mutt_b2s (LastDir), &state);
      browser_sort (&state);
    }
    else
#endif
    {
      i = mutt_buffer_len (LastDir);
      while (i && mutt_b2s (LastDir)[--i] == '/')
        LastDir->data[i] = '\0';
      mutt_buffer_fix_dptr (LastDir);
      if (!*(mutt_b2s (LastDir)))
        mutt_getcwd (LastDir);
    }
  }

  mutt_buffer_clear (f);

  if (state.buffy)
  {
    if (examine_mailboxes (NULL, &state) == -1)
      goto bail;
  }
  else
#ifdef USE_IMAP
    if (!state.imap_browse)
#endif
      if (examine_directory (NULL, &state, mutt_b2s (LastDir), mutt_b2s (prefix)) == -1)
        goto bail;

  menu = mutt_new_menu (MENU_FOLDER);
  menu->make_entry = folder_entry;
  menu->search = select_file_search;
  menu->title = title;
  menu->data = state.entry;
  if (multiple)
    menu->tag = file_tag;

  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_FOLDER,
                                  FolderHelp);
  mutt_push_current_menu (menu);

  init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));

  FOREVER
  {
    op = mutt_menuLoop (menu);

    if (state.entrylen)
      mutt_buffer_strcpy (defaultsel, state.entry[menu->current].full_path);

    switch (op)
    {
      case OP_DESCEND_DIRECTORY:
      case OP_GENERIC_SELECT_ENTRY:

	if (!state.entrylen)
	{
	  mutt_error _("No files match the file mask");
	  break;
	}

        if (S_ISDIR (state.entry[menu->current].mode) ||
	    (S_ISLNK (state.entry[menu->current].mode) &&
             link_is_dir (state.entry[menu->current].full_path))
#ifdef USE_IMAP
	    || state.entry[menu->current].inferiors
#endif
          )
	{
	  if (op == OP_DESCEND_DIRECTORY
              || (mx_get_magic (state.entry[menu->current].full_path) <= 0)
#ifdef USE_IMAP
              || state.entry[menu->current].inferiors
#endif
	    )
	  {
	    /* save the old directory */
	    mutt_buffer_strcpy (OldLastDir, mutt_b2s (LastDir));

            mutt_buffer_strcpy (defaultsel, mutt_b2s (OldLastDir));
            if (mutt_buffer_len (defaultsel) && (*(defaultsel->dptr - 1) == '/'))
            {
              defaultsel->dptr--;
              *(defaultsel->dptr) = '\0';
            }

	    if (mutt_strcmp (state.entry[menu->current].display_name, "..") == 0)
	    {
              size_t lastdirlen = mutt_buffer_len (LastDir);

	      if ((lastdirlen > 1) &&
                  mutt_strcmp ("..", mutt_b2s (LastDir) + lastdirlen - 2) == 0)
              {
		mutt_buffer_addstr (LastDir, "/..");
              }
	      else
	      {
		char *p = NULL;
                if (lastdirlen > 1)
                {
                  /* "mutt_b2s (LastDir) + 1" triggers a compiler warning */
                  p = strrchr (LastDir->data + 1, '/');
                }

		if (p)
                {
		  *p = 0;
                  mutt_buffer_fix_dptr (LastDir);
                }
		else
		{
		  if (mutt_b2s (LastDir)[0] == '/')
                    mutt_buffer_strcpy (LastDir, "/");
		  else
		    mutt_buffer_addstr (LastDir, "/..");
		}
	      }
	    }
	    else if (state.buffy)
	    {
	      mutt_buffer_strcpy (LastDir, state.entry[menu->current].full_path);
	    }
#ifdef USE_IMAP
	    else if (state.imap_browse)
	    {
	      ciss_url_t url;

              mutt_buffer_strcpy (LastDir, state.entry[menu->current].full_path);
	      /* tack on delimiter here */

	      /* special case "" needs no delimiter */
	      url_parse_ciss (&url, state.entry[menu->current].full_path);
	      if (url.path &&
		  (state.entry[menu->current].delim != '\0'))
	      {
                mutt_buffer_addch (LastDir, state.entry[menu->current].delim);
	      }
	    }
#endif
	    else
	    {
	      mutt_buffer_strcpy (LastDir, state.entry[menu->current].full_path);
	    }

	    destroy_state (&state);
	    if (killPrefix)
	    {
	      mutt_buffer_clear (prefix);
	      killPrefix = 0;
	    }
	    state.buffy = 0;
#ifdef USE_IMAP
	    if (state.imap_browse)
	    {
	      init_state (&state, NULL);
	      state.imap_browse = 1;
	      imap_browse (mutt_b2s (LastDir), &state);
	      browser_sort (&state);
	      menu->data = state.entry;
	    }
	    else
#endif
              if (examine_directory (menu, &state, mutt_b2s (LastDir), mutt_b2s (prefix)) == -1)
              {
                /* try to restore the old values */
                mutt_buffer_strcpy (LastDir, mutt_b2s (OldLastDir));
                if (examine_directory (menu, &state, mutt_b2s (LastDir), mutt_b2s (prefix)) == -1)
                {
                  mutt_buffer_strcpy (LastDir, NONULL(Homedir));
                  goto bail;
                }
              }
	    menu->current = 0;
	    menu->top = 0;
	    init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	    break;
	  }
	}
        else if (op == OP_DESCEND_DIRECTORY)
        {
          mutt_error (_("%s is not a directory."), state.entry[menu->current].display_name);
          break;
        }

        mutt_buffer_strcpy (f, state.entry[menu->current].full_path);

	/* fall through */

      case OP_EXIT:

	if (multiple)
	{
	  char **tfiles;

	  if (menu->tagged)
	  {
	    *numfiles = menu->tagged;
	    tfiles = safe_calloc (*numfiles, sizeof (char *));
	    for (i = 0, j = 0; i < state.entrylen; i++)
	      if (state.entry[i].tagged)
		tfiles[j++] = safe_strdup (state.entry[i].full_path);
	    *files = tfiles;
	  }
	  else if ((mutt_b2s (f))[0]) /* no tagged entries. return selected entry */
	  {
	    *numfiles = 1;
	    tfiles = safe_calloc (*numfiles, sizeof (char *));
	    tfiles[0] = safe_strdup (mutt_b2s (f));
	    *files = tfiles;
	  }
	}

	destroy_state (&state);
	goto bail;

      case OP_BROWSER_TELL:
        if (state.entrylen)
        {
          BUFFER *clean = mutt_buffer_pool_get ();
          mutt_buffer_remove_path_password (clean,
                                            state.entry[menu->current].full_path);
	  mutt_message("%s", mutt_b2s (clean));
          mutt_buffer_pool_release (&clean);
        }
        break;

#ifdef USE_IMAP
      case OP_BROWSER_SUBSCRIBE:
	imap_subscribe (state.entry[menu->current].full_path, 1);
	break;

      case OP_BROWSER_UNSUBSCRIBE:
	imap_subscribe (state.entry[menu->current].full_path, 0);
	break;

      case OP_BROWSER_TOGGLE_LSUB:
	if (option (OPTIMAPLSUB))
	  unset_option (OPTIMAPLSUB);
	else
	  set_option (OPTIMAPLSUB);

	mutt_unget_event (0, OP_CHECK_NEW);
	break;

      case OP_CREATE_MAILBOX:
	if (!state.imap_browse)
	{
	  mutt_error (_("Create is only supported for IMAP mailboxes"));
	  break;
	}

	if (!imap_mailbox_create (mutt_b2s (LastDir), defaultsel))
	{
	  /* TODO: find a way to detect if the new folder would appear in
	   *   this window, and insert it without starting over. */
	  destroy_state (&state);
	  init_state (&state, NULL);
	  state.imap_browse = 1;
	  imap_browse (mutt_b2s (LastDir), &state);
	  browser_sort (&state);
	  menu->data = state.entry;
	  menu->current = 0;
	  menu->top = 0;
	  init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	}
	/* else leave error on screen */
	break;

      case OP_RENAME_MAILBOX:
	if (!state.entry[menu->current].imap)
	  mutt_error (_("Rename is only supported for IMAP mailboxes"));
	else
	{
	  int nentry = menu->current;

	  if (imap_mailbox_rename (state.entry[nentry].full_path, defaultsel) >= 0)
	  {
	    destroy_state (&state);
	    init_state (&state, NULL);
	    state.imap_browse = 1;
	    imap_browse (mutt_b2s (LastDir), &state);
	    browser_sort (&state);
	    menu->data = state.entry;
	    menu->current = 0;
	    menu->top = 0;
	    init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	  }
	}
	break;

      case OP_DELETE_MAILBOX:
	if (!state.entry[menu->current].imap)
	  mutt_error (_("Delete is only supported for IMAP mailboxes"));
	else
        {
	  char msg[SHORT_STRING];
	  IMAP_MBOX mx;
	  int nentry = menu->current;

	  imap_parse_path (state.entry[nentry].full_path, &mx);
	  if (!mx.mbox)
	  {
	    mutt_error _("Cannot delete root folder");
	    break;
	  }
	  snprintf (msg, sizeof (msg), _("Really delete mailbox \"%s\"?"),
                    mx.mbox);
	  if (mutt_yesorno (msg, MUTT_NO) == MUTT_YES)
          {
	    if (!imap_delete_mailbox (Context, mx))
            {
	      /* free the mailbox from the browser */
	      FREE (&((state.entry)[nentry].display_name));
	      FREE (&((state.entry)[nentry].full_path));
	      /* and move all other entries up */
	      if (nentry+1 < state.entrylen)
		memmove (state.entry + nentry, state.entry + nentry + 1,
                         sizeof (struct folder_file) * (state.entrylen - (nentry+1)));
              memset (&state.entry[state.entrylen - 1], 0,
                      sizeof (struct folder_file));
	      state.entrylen--;
	      mutt_message _("Mailbox deleted.");
              mutt_buffer_clear (defaultsel);
	      init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	    }
            else
              mutt_error _("Mailbox deletion failed.");
	  }
	  else
	    mutt_message _("Mailbox not deleted.");
	  FREE (&mx.mbox);
        }
        break;
#endif

      case OP_CHANGE_DIRECTORY:

	mutt_buffer_strcpy (buf, mutt_b2s (LastDir));
        mutt_buffer_clear (defaultsel);
#ifdef USE_IMAP
	if (!state.imap_browse)
#endif
	{
	  /* add '/' at the end of the directory name if not already there */
	  size_t len = mutt_buffer_len (LastDir);
	  if (len && (mutt_b2s (LastDir)[len-1] != '/'))
	    mutt_buffer_addch (buf, '/');
	}

        /* buf comes from the buffer pool, so defaults to size LONG_STRING */
	if ((mutt_buffer_get_field (_("Chdir to: "), buf, MUTT_FILE) == 0) &&
	    mutt_buffer_len (buf))
	{
	  state.buffy = 0;
          /* no relative path expansion, because that should be compared
           * to LastDir, not cwd */
	  mutt_buffer_expand_path_norel (buf);
#ifdef USE_IMAP
	  if (mx_is_imap (mutt_b2s (buf)))
	  {
	    mutt_buffer_strcpy (LastDir, mutt_b2s (buf));
	    destroy_state (&state);
	    init_state (&state, NULL);
	    state.imap_browse = 1;
	    imap_browse (mutt_b2s (LastDir), &state);
	    browser_sort (&state);
	    menu->data = state.entry;
	    menu->current = 0;
	    menu->top = 0;
	    init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	  }
	  else
#endif
	  {
	    if (*(mutt_b2s (buf)) != '/')
	    {
	      /* in case dir is relative, make it relative to LastDir,
	       * not current working dir */
	      mutt_buffer_concat_path (tmp, mutt_b2s (LastDir), mutt_b2s (buf));
	      mutt_buffer_strcpy (buf, mutt_b2s (tmp));
	    }
	    if (stat (mutt_b2s (buf), &st) == 0)
	    {
	      if (S_ISDIR (st.st_mode))
	      {
		destroy_state (&state);
		if (examine_directory (menu, &state, mutt_b2s (buf), mutt_b2s (prefix)) == 0)
		  mutt_buffer_strcpy (LastDir, mutt_b2s (buf));
		else
		{
		  mutt_error _("Error scanning directory.");
		  if (examine_directory (menu, &state, mutt_b2s (LastDir), mutt_b2s (prefix)) == -1)
		  {
		    goto bail;
		  }
		}
		menu->current = 0;
		menu->top = 0;
		init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	      }
	      else
		mutt_error (_("%s is not a directory."), mutt_b2s (buf));
	    }
	    else
	      mutt_perror (mutt_b2s (buf));
	  }
	}
	break;

      case OP_ENTER_MASK:

	mutt_buffer_strcpy (buf, NONULL(Mask.pattern));
        /* buf comes from the buffer pool, so defaults to size LONG_STRING */
	if (mutt_buffer_get_field (_("File Mask: "), buf, 0) == 0)
	{
	  regex_t *rx = (regex_t *) safe_malloc (sizeof (regex_t));
	  const char *s = mutt_b2s (buf);
	  int not = 0, err;

	  state.buffy = 0;
	  /* assume that the user wants to see everything */
	  if (!(mutt_buffer_len (buf)))
	    mutt_buffer_strcpy (buf, ".");
	  SKIPWS (s);
	  if (*s == '!')
	  {
	    s++;
	    SKIPWS (s);
	    not = 1;
	  }

	  if ((err = REGCOMP (rx, s, REG_NOSUB)) != 0)
	  {
	    regerror (err, rx, buf->data, buf->dsize);
            mutt_buffer_fix_dptr (buf);
	    FREE (&rx);
	    mutt_error ("%s", mutt_b2s (buf));
	  }
	  else
	  {
	    mutt_str_replace (&Mask.pattern, mutt_b2s (buf));
	    regfree (Mask.rx);
	    FREE (&Mask.rx);
	    Mask.rx = rx;
	    Mask.not = not;

	    destroy_state (&state);
#ifdef USE_IMAP
	    if (state.imap_browse)
	    {
	      init_state (&state, NULL);
	      state.imap_browse = 1;
	      imap_browse (mutt_b2s (LastDir), &state);
	      browser_sort (&state);
	      menu->data = state.entry;
	      init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	    }
	    else
#endif
              if (examine_directory (menu, &state, mutt_b2s (LastDir), NULL) == 0)
                init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
              else
              {
                mutt_error _("Error scanning directory.");
                goto bail;
              }
	    killPrefix = 0;
	    if (!state.entrylen)
	    {
	      mutt_error _("No files match the file mask");
	      break;
	    }
	  }
	}
	break;

      case OP_SORT:
      case OP_SORT_REVERSE:

      {
        if (select_sort (&state, op == OP_SORT_REVERSE) == 1)
        {
          browser_sort (&state);
          set_sticky_cursor (&state, menu, mutt_b2s (defaultsel));
          menu->redraw = REDRAW_FULL;
        }
        break;
      }

      case OP_TOGGLE_MAILBOXES:
	state.buffy = !state.buffy;
        menu->current = 0;
        /* fall through */

      case OP_CHECK_NEW:
	destroy_state (&state);
	mutt_buffer_clear (prefix);
	killPrefix = 0;

	if (state.buffy)
	{
	  if (examine_mailboxes (menu, &state) == -1)
	    goto bail;
	}
#ifdef USE_IMAP
	else if (mx_is_imap (mutt_b2s (LastDir)))
	{
	  init_state (&state, NULL);
	  state.imap_browse = 1;
	  imap_browse (mutt_b2s (LastDir), &state);
	  browser_sort (&state);
	  menu->data = state.entry;
	}
#endif
	else if (examine_directory (menu, &state, mutt_b2s (LastDir), mutt_b2s (prefix)) == -1)
	  goto bail;
	init_menu (&state, menu, title, sizeof (title), mutt_b2s (defaultsel));
	break;

      case OP_BUFFY_LIST:
	mutt_buffy_list ();
	break;

      case OP_BROWSER_NEW_FILE:

	mutt_buffer_printf (buf, "%s/", mutt_b2s (LastDir));
        /* buf comes from the buffer pool, so defaults to size LONG_STRING */
	if (mutt_buffer_get_field (_("New file name: "), buf, MUTT_FILE) == 0)
	{
	  mutt_buffer_strcpy (f, mutt_b2s (buf));
	  destroy_state (&state);
	  goto bail;
	}
	break;

      case OP_BROWSER_VIEW_FILE:
	if (!state.entrylen)
	{
	  mutt_error _("No files match the file mask");
	  break;
	}

#ifdef USE_IMAP
	if (state.entry[menu->current].selectable)
	{
	  mutt_buffer_strcpy (f, state.entry[menu->current].full_path);
	  destroy_state (&state);
	  goto bail;
	}
	else
#endif
          if (S_ISDIR (state.entry[menu->current].mode) ||
              (S_ISLNK (state.entry[menu->current].mode) &&
               link_is_dir (state.entry[menu->current].full_path)))
          {
            if (flags & MUTT_SEL_DIRECTORY)
            {
              mutt_buffer_strcpy (f, state.entry[menu->current].full_path);
              destroy_state (&state);
              goto bail;
            }
            else
            {
              mutt_error _("Can't view a directory");
              break;
            }
          }
          else
          {
            BODY *b;

            b = mutt_make_file_attach (state.entry[menu->current].full_path);
            if (b != NULL)
            {
              mutt_view_attachment (NULL, b, MUTT_REGULAR, NULL, NULL);
              mutt_free_body (&b);
              menu->redraw = REDRAW_FULL;
            }
            else
              mutt_error _("Error trying to view file");
          }
    }
  }

bail:
  mutt_buffer_pool_release (&buf);
  mutt_buffer_pool_release (&prefix);
  mutt_buffer_pool_release (&tmp);
  mutt_buffer_pool_release (&OldLastDir);
  mutt_buffer_pool_release (&defaultsel);

  if (menu)
  {
    mutt_pop_current_menu (menu);
    mutt_menuDestroy (&menu);
  }

  if (!folder)
    mutt_buffer_strcpy (LastDir, mutt_b2s (LastDirBackup));

}
