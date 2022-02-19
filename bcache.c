/*
 * Copyright (C) 2006-2007,2009,2017 Brendan Cully <brendan@kublai.com>
 * Copyright (C) 2006,2009 Rocco Rutte <pdmef@gmx.net>
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
#include "config.h"
#endif				/* HAVE_CONFIG_H */

#include <sys/types.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>

#include "mutt.h"
#include "account.h"
#include "url.h"
#include "bcache.h"

#include "lib.h"

struct body_cache {
  char *path;
};

static int bcache_path(ACCOUNT *account, const char *mailbox, body_cache_t *bcache)
{
  char host[STRING];
  BUFFER *path, *dst;
  ciss_url_t url;

  if (!account || !MessageCachedir || !bcache)
    return -1;

  /* make up a ciss_url_t we can turn into a string */
  memset (&url, 0, sizeof (ciss_url_t));
  /* force username in the url to ensure uniqueness */
  mutt_account_tourl (account, &url, 1);
  /*
   * mutt_account_tourl() just sets up some pointers;
   * if this ever changes, we have a memleak here
   */
  url.path = NULL;
  if (url_ciss_tostring (&url, host, sizeof (host), U_PATH) < 0)
  {
    dprint (1, (debugfile, "bcache_path: URL to string failed\n"));
    return -1;
  }

  path = mutt_buffer_pool_get ();
  dst = mutt_buffer_pool_get ();
  mutt_encode_path (path, NONULL (mailbox));

  mutt_buffer_printf (dst, "%s/%s%s", MessageCachedir, host, mutt_b2s (path));
  if (*(dst->dptr - 1) != '/')
    mutt_buffer_addch (dst, '/');

  dprint (3, (debugfile, "bcache_path: path: '%s'\n", mutt_b2s (dst)));
  bcache->path = safe_strdup (mutt_b2s (dst));

  mutt_buffer_pool_release (&path);
  mutt_buffer_pool_release (&dst);
  return 0;
}

body_cache_t *mutt_bcache_open (ACCOUNT *account, const char *mailbox)
{
  struct body_cache *bcache = NULL;

  if (!account)
    goto bail;

  bcache = safe_calloc (1, sizeof (struct body_cache));
  if (bcache_path (account, mailbox, bcache) < 0)
    goto bail;

  return bcache;

bail:
  mutt_bcache_close (&bcache);
  return NULL;
}

void mutt_bcache_close (body_cache_t **bcache)
{
  if (!bcache || !*bcache)
    return;
  FREE (&(*bcache)->path);
  FREE(bcache);			/* __FREE_CHECKED__ */
}

FILE* mutt_bcache_get(body_cache_t *bcache, const char *id)
{
  BUFFER *path;
  FILE* fp = NULL;

  if (!id || !*id || !bcache)
    return NULL;

  path = mutt_buffer_pool_get ();
  mutt_buffer_addstr (path, bcache->path);
  mutt_buffer_addstr (path, id);

  fp = safe_fopen (mutt_b2s (path), "r");

  dprint (3, (debugfile, "bcache: get: '%s': %s\n", mutt_b2s (path),
              fp == NULL ? "no" : "yes"));

  mutt_buffer_pool_release (&path);
  return fp;
}

FILE* mutt_bcache_put(body_cache_t *bcache, const char *id, int tmp)
{
  BUFFER *path = NULL;
  FILE* fp = NULL;
  char* s = NULL;
  struct stat sb;

  if (!id || !*id || !bcache)
    return NULL;

  path = mutt_buffer_pool_get ();
  mutt_buffer_printf (path, "%s%s%s", bcache->path, id,
                      tmp ? ".tmp" : "");

  if ((fp = safe_fopen (mutt_b2s (path), "w+")))
    goto out;

  if (errno == EEXIST)
    /* clean up leftover tmp file */
    mutt_unlink (mutt_b2s (path));

  if (mutt_buffer_len (path))
    s = strchr (path->data + 1, '/');
  while (!(fp = safe_fopen (mutt_b2s (path), "w+")) && errno == ENOENT && s)
  {
    /* create missing path components */
    *s = '\0';
    if (stat (mutt_b2s (path), &sb) < 0 &&
        (errno != ENOENT || mkdir (mutt_b2s (path), 0777) < 0))
      goto out;
    *s = '/';
    s = strchr (s + 1, '/');
  }

out:
  dprint (3, (debugfile, "bcache: put: '%s'\n", mutt_b2s (path)));
  mutt_buffer_pool_release (&path);
  return fp;
}

int mutt_bcache_commit(body_cache_t* bcache, const char* id)
{
  BUFFER *tmpid;
  int rv;

  tmpid = mutt_buffer_pool_get ();
  mutt_buffer_printf (tmpid, "%s.tmp", id);

  rv = mutt_bcache_move (bcache, mutt_b2s (tmpid), id);

  mutt_buffer_pool_release (&tmpid);
  return rv;
}

int mutt_bcache_move(body_cache_t* bcache, const char* id, const char* newid)
{
  BUFFER *path, *newpath;
  int rv;

  if (!bcache || !id || !*id || !newid || !*newid)
    return -1;

  path = mutt_buffer_pool_get ();
  newpath = mutt_buffer_pool_get ();

  mutt_buffer_printf (path, "%s%s", bcache->path, id);
  mutt_buffer_printf (newpath, "%s%s", bcache->path, newid);

  dprint (3, (debugfile, "bcache: mv: '%s' '%s'\n",
              mutt_b2s (path), mutt_b2s (newpath)));

  rv = rename (mutt_b2s (path), mutt_b2s (newpath));

  mutt_buffer_pool_release (&path);
  mutt_buffer_pool_release (&newpath);
  return rv;
}

int mutt_bcache_del(body_cache_t *bcache, const char *id)
{
  BUFFER *path;
  int rv;

  if (!id || !*id || !bcache)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_addstr (path, bcache->path);
  mutt_buffer_addstr (path, id);

  dprint (3, (debugfile, "bcache: del: '%s'\n", mutt_b2s (path)));

  rv = unlink (mutt_b2s (path));

  mutt_buffer_pool_release (&path);
  return rv;
}

int mutt_bcache_exists(body_cache_t *bcache, const char *id)
{
  BUFFER *path;
  struct stat st;
  int rc = 0;

  if (!id || !*id || !bcache)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_addstr (path, bcache->path);
  mutt_buffer_addstr (path, id);

  if (stat (mutt_b2s (path), &st) < 0)
    rc = -1;
  else
    rc = S_ISREG(st.st_mode) && st.st_size != 0 ? 0 : -1;

  dprint (3, (debugfile, "bcache: exists: '%s': %s\n",
              mutt_b2s (path), rc == 0 ? "yes" : "no"));

  mutt_buffer_pool_release (&path);
  return rc;
}

int mutt_bcache_list(body_cache_t *bcache,
		     int (*want_id)(const char *id, body_cache_t *bcache,
				    void *data), void *data)
{
  DIR *d = NULL;
  struct dirent *de;
  int rc = -1;

  if (!bcache || !(d = opendir (bcache->path)))
    goto out;

  rc = 0;

  dprint (3, (debugfile, "bcache: list: dir: '%s'\n", bcache->path));

  while ((de = readdir (d)))
  {
    if (mutt_strncmp (de->d_name, ".", 1) == 0 ||
	mutt_strncmp (de->d_name, "..", 2) == 0)
      continue;

    dprint (3, (debugfile, "bcache: list: dir: '%s', id :'%s'\n", bcache->path, de->d_name));

    if (want_id && want_id (de->d_name, bcache, data) != 0)
      goto out;

    rc++;
  }

out:
  if (d)
  {
    if (closedir (d) < 0)
      rc = -1;
  }
  dprint (3, (debugfile, "bcache: list: did %d entries\n", rc));
  return rc;
}
