/*
 * Copyright (C) 1996-2000,2007 Michael R. Elkins <me@mutt.org>
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
#ifdef USE_IMAP
#include "mailbox.h"
#include "imap.h"
#endif

#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* given a partial pathname, this routine fills in as much of the rest of the
 * path as is unique.
 *
 * return 0 if ok, -1 if no matches
 */
int mutt_complete (char *s, size_t slen)
{
  char *p;
  DIR *dirp = NULL;
  struct dirent *de;
  int i ,init=0;
  size_t len;
  BUFFER *dirpart = NULL;
  BUFFER *exp_dirpart = NULL;
  BUFFER *filepart = NULL;
  BUFFER *buf = NULL;

#ifdef USE_IMAP
  BUFFER *imap_path = NULL;
  int rc;

  dprint (2, (debugfile, "mutt_complete: completing %s\n", s));

  imap_path = mutt_buffer_pool_get ();
  /* we can use '/' as a delimiter, imap_complete rewrites it */
  if (*s == '=' || *s == '+' || *s == '!')
  {
    if (*s == '!')
      p = NONULL (Spoolfile);
    else
      p = NONULL (Maildir);

    mutt_buffer_concat_path (imap_path, p, s+1);
  }
  else
    mutt_buffer_strcpy (imap_path, s);

  if (mx_is_imap (mutt_b2s (imap_path)))
  {
    rc = imap_complete (s, slen, mutt_b2s (imap_path));
    mutt_buffer_pool_release (&imap_path);
    return rc;
  }

  mutt_buffer_pool_release (&imap_path);
#endif

  dirpart = mutt_buffer_pool_get ();
  exp_dirpart = mutt_buffer_pool_get ();
  filepart = mutt_buffer_pool_get ();
  buf = mutt_buffer_pool_get ();

  if (*s == '=' || *s == '+' || *s == '!')
  {
    mutt_buffer_addch (dirpart, *s);
    if (*s == '!')
      mutt_buffer_strcpy (exp_dirpart, NONULL (Spoolfile));
    else
      mutt_buffer_strcpy (exp_dirpart, NONULL (Maildir));
    if ((p = strrchr (s, '/')))
    {
      mutt_buffer_concatn_path (buf,
                                mutt_b2s (exp_dirpart), mutt_buffer_len (exp_dirpart),
                                s + 1, (size_t)(p - s - 1));
      mutt_buffer_strcpy (exp_dirpart, mutt_b2s (buf));
      mutt_buffer_substrcpy (dirpart, s, p+1);
      mutt_buffer_strcpy (filepart, p + 1);
    }
    else
      mutt_buffer_strcpy (filepart, s + 1);
    dirp = opendir (mutt_b2s (exp_dirpart));
  }
  else
  {
    if ((p = strrchr (s, '/')))
    {
      if (p == s) /* absolute path */
      {
	p = s + 1;
	mutt_buffer_strcpy (dirpart, "/");
	mutt_buffer_strcpy (filepart, p);
	dirp = opendir (mutt_b2s (dirpart));
      }
      else
      {
	mutt_buffer_substrcpy (dirpart, s, p);
	mutt_buffer_strcpy (filepart, p + 1);
	mutt_buffer_strcpy (exp_dirpart, mutt_b2s (dirpart));
	mutt_buffer_expand_path (exp_dirpart);
	dirp = opendir (mutt_b2s (exp_dirpart));
      }
    }
    else
    {
      /* no directory name, so assume current directory. */
      mutt_buffer_strcpy (filepart, s);
      dirp = opendir (".");
    }
  }

  if (dirp == NULL)
  {
    dprint (1, (debugfile, "mutt_complete(): %s: %s (errno %d).\n",
                mutt_b2s (exp_dirpart), strerror (errno), errno));
    goto cleanup;
  }

  /*
   * special case to handle when there is no filepart yet.  find the first
   * file/directory which is not ``.'' or ``..''
   */
  if ((len = mutt_buffer_len (filepart)) == 0)
  {
    while ((de = readdir (dirp)) != NULL)
    {
      if (mutt_strcmp (".", de->d_name) != 0 && mutt_strcmp ("..", de->d_name) != 0)
      {
	mutt_buffer_strcpy (filepart, de->d_name);
	init++;
	break;
      }
    }
  }

  while ((de = readdir (dirp)) != NULL)
  {
    if (mutt_strncmp (de->d_name, mutt_b2s (filepart), len) == 0)
    {
      if (init)
      {
        char *fpch;

	for (i=0, fpch = filepart->data; *fpch && de->d_name[i]; i++, fpch++)
	{
	  if (*fpch != de->d_name[i])
	    break;
	}
        *fpch = 0;
        mutt_buffer_fix_dptr (filepart);
      }
      else
      {
	struct stat st;

	mutt_buffer_strcpy (filepart, de->d_name);

	/* check to see if it is a directory */
	if (mutt_buffer_len (dirpart))
	{
	  mutt_buffer_strcpy (buf, mutt_b2s (exp_dirpart));
	  mutt_buffer_addch (buf, '/');
	}
	else
	  mutt_buffer_clear (buf);
	mutt_buffer_addstr (buf, mutt_b2s (filepart));
	if (stat (mutt_b2s (buf), &st) != -1 && (st.st_mode & S_IFDIR))
          mutt_buffer_addch (filepart, '/');
	init = 1;
      }
    }
  }
  closedir (dirp);

  if (mutt_buffer_len (dirpart))
  {
    strfcpy (s, mutt_b2s (dirpart), slen);
    if (mutt_strcmp ("/", mutt_b2s (dirpart)) != 0 &&
        mutt_b2s (dirpart)[0] != '=' &&
        mutt_b2s (dirpart)[0] != '+')
      strfcpy (s + strlen (s), "/", slen - strlen (s));
    strfcpy (s + strlen (s), mutt_b2s (filepart), slen - strlen (s));
  }
  else
    strfcpy (s, mutt_b2s (filepart), slen);

cleanup:
  mutt_buffer_pool_release (&dirpart);
  mutt_buffer_pool_release (&exp_dirpart);
  mutt_buffer_pool_release (&filepart);
  mutt_buffer_pool_release (&buf);

  return (init ? 0 : -1);
}
