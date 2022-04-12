/*
 * Copyright (C) 2004 Thomas Glanzmann <sithglan@stud.uni-erlangen.de>
 * Copyright (C) 2004 Tobias Werth <sitowert@stud.uni-erlangen.de>
 * Copyright (C) 2004 Brian Fundakowski Feldman <green@FreeBSD.org>
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

#if HAVE_QDBM
#include <depot.h>
#include <cabin.h>
#include <villa.h>
#elif HAVE_TC
#include <tcbdb.h>
#elif HAVE_KC
#include <kclangc.h>
#elif HAVE_GDBM
#include <gdbm.h>
#elif HAVE_DB4
#include <db.h>
#elif HAVE_LMDB
/* This is the maximum size of the database file (2GiB) which is
 * mmap(2)'ed into memory.  This limit should be good for ~800,000
 * emails. */
#define LMDB_DB_SIZE    2147483648
#include <lmdb.h>
#endif

#include <errno.h>
#include <fcntl.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include "mutt.h"
#include "hcache.h"
#include "hcversion.h"
#include "mx.h"
#include "lib.h"
#include "md5.h"
#include "rfc822.h"

unsigned int hcachever = 0x0;

#if HAVE_QDBM
struct header_cache
{
  VILLA *db;
  char *folder;
  unsigned int crc;
};
#elif HAVE_TC
struct header_cache
{
  TCBDB *db;
  char *folder;
  unsigned int crc;
};
#elif HAVE_KC
struct header_cache
{
  KCDB *db;
  char *folder;
  unsigned int crc;
};
#elif HAVE_GDBM
struct header_cache
{
  GDBM_FILE db;
  char *folder;
  unsigned int crc;
};
#elif HAVE_DB4
struct header_cache
{
  DB_ENV *env;
  DB *db;
  char *folder;
  unsigned int crc;
  int fd;
  BUFFER *lockfile;
};

static void mutt_hcache_dbt_init(DBT * dbt, void *data, size_t len);
static void mutt_hcache_dbt_empty_init(DBT * dbt);
#elif HAVE_LMDB
enum mdb_txn_mode
{
  txn_uninitialized = 0,
  txn_read,
  txn_write
};
struct header_cache
{
  MDB_env *env;
  MDB_txn *txn;
  MDB_dbi db;
  char *folder;
  unsigned int crc;
  enum mdb_txn_mode txn_mode;
};

static int mdb_get_r_txn(header_cache_t *h)
{
  int rc;

  if (h->txn)
  {
    if (h->txn_mode == txn_read || h->txn_mode == txn_write)
      return MDB_SUCCESS;

    if ((rc = mdb_txn_renew (h->txn)) != MDB_SUCCESS)
    {
      h->txn = NULL;
      dprint (2, (debugfile, "mdb_get_r_txn: mdb_txn_renew: %s\n",
                  mdb_strerror (rc)));
      return rc;
    }
    h->txn_mode = txn_read;
    return rc;
  }

  if ((rc = mdb_txn_begin (h->env, NULL, MDB_RDONLY, &h->txn)) != MDB_SUCCESS)
  {
    h->txn = NULL;
    dprint (2, (debugfile, "mdb_get_r_txn: mdb_txn_begin: %s\n",
                mdb_strerror (rc)));
    return rc;
  }
  h->txn_mode = txn_read;
  return rc;
}

static int mdb_get_w_txn(header_cache_t *h)
{
  int rc;

  if (h->txn)
  {
    if (h->txn_mode == txn_write)
      return MDB_SUCCESS;

    /* Free up the memory for readonly or reset transactions */
    mdb_txn_abort (h->txn);
  }

  if ((rc = mdb_txn_begin (h->env, NULL, 0, &h->txn)) != MDB_SUCCESS)
  {
    h->txn = NULL;
    dprint (2, (debugfile, "mdb_get_w_txn: mdb_txn_begin %s\n",
                mdb_strerror (rc)));
    return rc;
  }

  h->txn_mode = txn_write;
  return rc;
}
#endif

typedef union
{
  struct timeval timeval;
  unsigned int uidvalidity;
} validate;

static void *
lazy_malloc(size_t siz)
{
  if (0 < siz && siz < 4096)
    siz = 4096;

  return safe_malloc(siz);
}

static void
lazy_realloc(void *ptr, size_t siz)
{
  void **p = (void **) ptr;

  if (p != NULL && 0 < siz && siz < 4096)
    return;

  safe_realloc(ptr, siz);
}

static unsigned char *
dump_int(unsigned int i, unsigned char *d, int *off)
{
  lazy_realloc(&d, *off + sizeof (int));
  memcpy(d + *off, &i, sizeof (int));
  (*off) += sizeof (int);

  return d;
}

static void
restore_int(unsigned int *i, const unsigned char *d, int *off)
{
  memcpy(i, d + *off, sizeof (int));
  (*off) += sizeof (int);
}

static inline int is_ascii (const char *p, size_t len)
{
  register const char *s = p;
  while (s && (unsigned) (s - p) < len)
  {
    if ((*s & 0x80) != 0)
      return 0;
    s++;
  }
  return 1;
}

static unsigned char *
dump_char_size(char *c, unsigned char *d, int *off, ssize_t size, int convert)
{
  char *p = c;

  if (c == NULL)
  {
    size = 0;
    d = dump_int(size, d, off);
    return d;
  }

  if (convert && !is_ascii (c, size))
  {
    p = mutt_substrdup (c, c + size);
    if (mutt_convert_string (&p, Charset, "utf-8", 0) == 0)
    {
      c = p;
      size = mutt_strlen (c) + 1;
    }
  }

  d = dump_int(size, d, off);
  lazy_realloc(&d, *off + size);
  memcpy(d + *off, p, size);
  *off += size;

  if (p != c)
    FREE(&p);

  return d;
}

static unsigned char *
dump_char(char *c, unsigned char *d, int *off, int convert)
{
  return dump_char_size (c, d, off, mutt_strlen (c) + 1, convert);
}

static void
restore_char(char **c, const unsigned char *d, int *off, int convert)
{
  unsigned int size;
  restore_int(&size, d, off);

  if (size == 0)
  {
    *c = NULL;
    return;
  }

  *c = safe_malloc(size);
  memcpy(*c, d + *off, size);
  if (convert && !is_ascii (*c, size))
  {
    char *tmp = safe_strdup (*c);
    if (mutt_convert_string (&tmp, "utf-8", Charset, 0) == 0)
    {
      mutt_str_replace (c, tmp);
    }
    else
    {
      FREE(&tmp);
    }
  }
  *off += size;
}

static unsigned char *
dump_address(ADDRESS * a, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (a)
  {
#ifdef EXACT_ADDRESS
    d = dump_char(a->val, d, off, convert);
#endif
    d = dump_char(a->personal, d, off, convert);
    d = dump_char(a->mailbox, d, off, 0);
    d = dump_int(a->group, d, off);
    a = a->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_address(ADDRESS ** a, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *a = rfc822_new_address();
#ifdef EXACT_ADDRESS
    restore_char(&(*a)->val, d, off, convert);
#endif
    restore_char(&(*a)->personal, d, off, convert);
    restore_char(&(*a)->mailbox, d, off, 0);
    restore_int((unsigned int *) &(*a)->group, d, off);
    a = &(*a)->next;
    counter--;
  }

  *a = NULL;
}

static unsigned char *
dump_list(LIST * l, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (l)
  {
    d = dump_char(l->data, d, off, convert);
    l = l->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_list(LIST ** l, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *l = safe_malloc(sizeof (LIST));
    restore_char(&(*l)->data, d, off, convert);
    l = &(*l)->next;
    counter--;
  }

  *l = NULL;
}

static unsigned char *
dump_buffer(BUFFER * b, unsigned char *d, int *off, int convert)
{
  if (!b)
  {
    d = dump_int(0, d, off);
    return d;
  }
  else
    d = dump_int(1, d, off);

  d = dump_char_size(b->data, d, off, b->dsize + 1, convert);
  d = dump_int(b->dptr - b->data, d, off);
  d = dump_int(b->dsize, d, off);

  return d;
}

static void
restore_buffer(BUFFER ** b, const unsigned char *d, int *off, int convert)
{
  unsigned int used;
  unsigned int offset;
  restore_int(&used, d, off);
  if (!used)
  {
    return;
  }

  *b = safe_malloc(sizeof (BUFFER));

  restore_char(&(*b)->data, d, off, convert);
  restore_int(&offset, d, off);
  (*b)->dptr = (*b)->data + offset;
  restore_int (&used, d, off);
  (*b)->dsize = used;
}

static unsigned char *
dump_parameter(PARAMETER * p, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (p)
  {
    d = dump_char(p->attribute, d, off, 0);
    d = dump_char(p->value, d, off, convert);
    p = p->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_parameter(PARAMETER ** p, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *p = safe_malloc(sizeof (PARAMETER));
    restore_char(&(*p)->attribute, d, off, 0);
    restore_char(&(*p)->value, d, off, convert);
    p = &(*p)->next;
    counter--;
  }

  *p = NULL;
}

static unsigned char *
dump_body(BODY * c, unsigned char *d, int *off, int convert)
{
  BODY nb;

  memcpy (&nb, c, sizeof (BODY));

  /* some fields are not safe to cache */
  nb.content = NULL;
  nb.charset = NULL;
  nb.next = NULL;
  nb.parts = NULL;
  nb.hdr = NULL;
  nb.aptr = NULL;
  nb.mime_headers = NULL;

  lazy_realloc(&d, *off + sizeof (BODY));
  memcpy(d + *off, &nb, sizeof (BODY));
  *off += sizeof (BODY);

  d = dump_char(nb.xtype, d, off, 0);
  d = dump_char(nb.subtype, d, off, 0);

  d = dump_parameter(nb.parameter, d, off, convert);

  d = dump_char(nb.description, d, off, convert);
  d = dump_char(nb.form_name, d, off, convert);
  d = dump_char(nb.filename, d, off, convert);
  d = dump_char(nb.d_filename, d, off, convert);

  return d;
}

static void
restore_body(BODY * c, const unsigned char *d, int *off, int convert)
{
  memcpy(c, d + *off, sizeof (BODY));
  *off += sizeof (BODY);

  restore_char(&c->xtype, d, off, 0);
  restore_char(&c->subtype, d, off, 0);

  restore_parameter(&c->parameter, d, off, convert);

  restore_char(&c->description, d, off, convert);
  restore_char(&c->form_name, d, off, convert);
  restore_char(&c->filename, d, off, convert);
  restore_char(&c->d_filename, d, off, convert);
}

static unsigned char *
dump_envelope(ENVELOPE * e, unsigned char *d, int *off, int convert)
{
  d = dump_address(e->return_path, d, off, convert);
  d = dump_address(e->from, d, off, convert);
  d = dump_address(e->to, d, off, convert);
  d = dump_address(e->cc, d, off, convert);
  d = dump_address(e->bcc, d, off, convert);
  d = dump_address(e->sender, d, off, convert);
  d = dump_address(e->reply_to, d, off, convert);
  d = dump_address(e->mail_followup_to, d, off, convert);

  d = dump_char(e->list_post, d, off, convert);
  d = dump_char(e->subject, d, off, convert);

  if (e->real_subj)
    d = dump_int(e->real_subj - e->subject, d, off);
  else
    d = dump_int(-1, d, off);

  d = dump_char(e->message_id, d, off, 0);
  d = dump_char(e->supersedes, d, off, 0);
  d = dump_char(e->date, d, off, 0);
  d = dump_char(e->x_label, d, off, convert);

  d = dump_buffer(e->spam, d, off, convert);

  d = dump_list(e->references, d, off, 0);
  d = dump_list(e->in_reply_to, d, off, 0);
  d = dump_list(e->userhdrs, d, off, convert);

  return d;
}

static void
restore_envelope(ENVELOPE * e, const unsigned char *d, int *off, int convert)
{
  int real_subj_off;

  restore_address(&e->return_path, d, off, convert);
  restore_address(&e->from, d, off, convert);
  restore_address(&e->to, d, off, convert);
  restore_address(&e->cc, d, off, convert);
  restore_address(&e->bcc, d, off, convert);
  restore_address(&e->sender, d, off, convert);
  restore_address(&e->reply_to, d, off, convert);
  restore_address(&e->mail_followup_to, d, off, convert);

  restore_char(&e->list_post, d, off, convert);

  if (option (OPTAUTOSUBSCRIBE))
    mutt_auto_subscribe (e->list_post);

  restore_char(&e->subject, d, off, convert);
  restore_int((unsigned int *) (&real_subj_off), d, off);

  if (0 <= real_subj_off)
    e->real_subj = e->subject + real_subj_off;
  else
    e->real_subj = NULL;

  restore_char(&e->message_id, d, off, 0);
  restore_char(&e->supersedes, d, off, 0);
  restore_char(&e->date, d, off, 0);
  restore_char(&e->x_label, d, off, convert);

  restore_buffer(&e->spam, d, off, convert);

  restore_list(&e->references, d, off, 0);
  restore_list(&e->in_reply_to, d, off, 0);
  restore_list(&e->userhdrs, d, off, convert);
}

static int
crc_matches(const char *d, unsigned int crc)
{
  int off = sizeof (validate);
  unsigned int mycrc = 0;

  if (!d)
    return 0;

  restore_int(&mycrc, (unsigned char *) d, &off);

  return (crc == mycrc);
}

/* Append md5sumed folder to path if path is a directory. */
void
mutt_hcache_per_folder(BUFFER *hcpath, const char *path, const char *folder,
                       hcache_namer_t namer)
{
  BUFFER *hcfile = NULL;
  struct stat sb;
  unsigned char md5sum[16];
  char* s;
  int ret;
  size_t plen;
#ifndef HAVE_ICONV
  const char *chs = Charset ? Charset : mutt_get_default_charset ();
#endif

  plen = mutt_strlen (path);

  ret = stat(path, &sb);
  if (ret < 0 && path[plen-1] != '/')
  {
#ifdef HAVE_ICONV
    mutt_buffer_strcpy (hcpath, path);
#else
    mutt_buffer_printf (hcpath, "%s-%s", path, chs);
#endif
    return;
  }

  if (ret >= 0 && !S_ISDIR(sb.st_mode))
  {
#ifdef HAVE_ICONV
    mutt_buffer_strcpy (hcpath, path);
#else
    mutt_buffer_printf (hcpath, "%s-%s", path, chs);
#endif
    return;
  }

  hcfile = mutt_buffer_pool_get ();

  if (namer)
  {
    namer (folder, hcfile);
  }
  else
  {
    md5_buffer (folder, strlen (folder), &md5sum);
    mutt_buffer_printf(hcfile,
                       "%02x%02x%02x%02x%02x%02x%02x%02x"
                       "%02x%02x%02x%02x%02x%02x%02x%02x",
                       md5sum[0], md5sum[1], md5sum[2], md5sum[3],
                       md5sum[4], md5sum[5], md5sum[6], md5sum[7],
                       md5sum[8], md5sum[9], md5sum[10], md5sum[11],
                       md5sum[12], md5sum[13], md5sum[14], md5sum[15]);
#ifndef HAVE_ICONV
    mutt_buffer_addch (hcfile, '-');
    mutt_buffer_addstr (hcfile, chs);
#endif
  }

  mutt_buffer_concat_path (hcpath, path, mutt_b2s (hcfile));
  mutt_buffer_pool_release (&hcfile);

  if (stat (mutt_b2s (hcpath), &sb) >= 0)
    return;

  s = strchr (hcpath->data + 1, '/');
  while (s)
  {
    /* create missing path components */
    *s = '\0';
    if (stat (mutt_b2s (hcpath), &sb) < 0 &&
        (errno != ENOENT || mkdir (mutt_b2s (hcpath), 0777) < 0))
    {
      mutt_buffer_strcpy (hcpath, path);
      break;
    }
    *s = '/';
    s = strchr (s + 1, '/');
  }
}

/* This function transforms a header into a char so that it is usable by
 * db_store.
 */
static void *
mutt_hcache_dump(header_cache_t *h, HEADER * header, int *off,
		 unsigned int uidvalidity, mutt_hcache_store_flags_t flags)
{
  unsigned char *d = NULL;
  HEADER nh;
  int convert = !Charset_is_utf8;

  *off = 0;
  d = lazy_malloc(sizeof (validate));

  if (flags & MUTT_GENERATE_UIDVALIDITY)
  {
    struct timeval now;
    gettimeofday(&now, NULL);
    memcpy(d, &now, sizeof (struct timeval));
  }
  else
    memcpy(d, &uidvalidity, sizeof (uidvalidity));
  *off += sizeof (validate);

  d = dump_int(h->crc, d, off);

  lazy_realloc(&d, *off + sizeof (HEADER));
  memcpy(&nh, header, sizeof (HEADER));

  /* some fields are not safe to cache */
  nh.tagged = 0;
  nh.changed = 0;
  nh.threaded = 0;
  nh.recip_valid = 0;
  nh.searched = 0;
  nh.matched = 0;
  nh.collapsed = 0;
  nh.limited = 0;
  nh.num_hidden = 0;
  nh.recipient = 0;
  nh.color.pair = 0;
  nh.color.attrs = 0;
  nh.attach_valid = 0;
  nh.path = NULL;
  nh.tree = NULL;
  nh.thread = NULL;
#ifdef MIXMASTER
  nh.chain = NULL;
#endif
#if defined USE_POP || defined USE_IMAP
  nh.data = NULL;
#endif

  memcpy(d + *off, &nh, sizeof (HEADER));
  *off += sizeof (HEADER);

  d = dump_envelope(nh.env, d, off, convert);
  d = dump_body(nh.content, d, off, convert);
  d = dump_char(nh.maildir_flags, d, off, convert);

  return d;
}

HEADER *
mutt_hcache_restore(const unsigned char *d, HEADER ** oh)
{
  int off = 0;
  HEADER *h = mutt_new_header();
  int convert = !Charset_is_utf8;

  /* skip validate */
  off += sizeof (validate);

  /* skip crc */
  off += sizeof (unsigned int);

  memcpy(h, d + off, sizeof (HEADER));
  off += sizeof (HEADER);

  h->env = mutt_new_envelope();
  restore_envelope(h->env, d, &off, convert);

  h->content = mutt_new_body();
  restore_body(h->content, d, &off, convert);

  restore_char(&h->maildir_flags, d, &off, convert);

  /* this is needed for maildir style mailboxes */
  if (oh)
  {
    h->old = (*oh)->old;
    h->path = safe_strdup((*oh)->path);
    mutt_free_header(oh);
  }

  return h;
}

void *
mutt_hcache_fetch(header_cache_t *h, const char *filename,
		  size_t(*keylen) (const char *fn))
{
  void* data;

  data = mutt_hcache_fetch_raw (h, filename, keylen);

  if (!data || !crc_matches(data, h->crc))
  {
    mutt_hcache_free (&data);
    return NULL;
  }

  return data;
}

void *
mutt_hcache_fetch_raw (header_cache_t *h, const char *filename,
                       size_t(*keylen) (const char *fn))
{
#ifndef HAVE_DB4
  BUFFER *path = NULL;
  int ksize;
  void *rv = NULL;
#endif
#if HAVE_TC
  int sp;
#elif HAVE_KC
  size_t sp;
#elif HAVE_GDBM
  datum key;
  datum data;
#elif HAVE_DB4
  DBT key;
  DBT data;
#elif HAVE_LMDB
  MDB_val key;
  MDB_val data;
#endif

  if (!h)
    return NULL;

#ifdef HAVE_DB4
  if (filename[0] == '/')
    filename++;

  mutt_hcache_dbt_init(&key, (void *) filename, keylen(filename));
  mutt_hcache_dbt_empty_init(&data);
  data.flags = DB_DBT_MALLOC;

  h->db->get(h->db, NULL, &key, &data, 0);

  return data.data;

#else
  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  ksize = strlen (h->folder) + keylen (filename);

#ifdef HAVE_QDBM
  rv = vlget(h->db, mutt_b2s (path), ksize, NULL);
#elif HAVE_TC
  rv = tcbdbget(h->db, mutt_b2s (path), ksize, &sp);
#elif HAVE_KC
  rv = kcdbget(h->db, mutt_b2s (path), ksize, &sp);
#elif HAVE_GDBM
  key.dptr = path->data;
  key.dsize = ksize;

  data = gdbm_fetch(h->db, key);

  rv = data.dptr;
#elif HAVE_LMDB
  key.mv_data = path->data;
  key.mv_size = ksize;
  /* LMDB claims ownership of the returned data, so this will not be
   * freed in mutt_hcache_free(). */
  if ((mdb_get_r_txn (h) == MDB_SUCCESS) &&
      (mdb_get (h->txn, h->db, &key, &data) == MDB_SUCCESS))
    rv = data.mv_data;
#endif

  mutt_buffer_pool_release (&path);
  return rv;
#endif
}

/*
 * flags
 *
 * MUTT_GENERATE_UIDVALIDITY
 * ignore uidvalidity param and store gettimeofday() as the value
 */
int
mutt_hcache_store(header_cache_t *h, const char *filename, HEADER * header,
		  unsigned int uidvalidity,
		  size_t(*keylen) (const char *fn),
		  mutt_hcache_store_flags_t flags)
{
  char* data;
  int dlen;
  int ret;

  if (!h)
    return -1;

  data = mutt_hcache_dump(h, header, &dlen, uidvalidity, flags);
  ret = mutt_hcache_store_raw (h, filename, data, dlen, keylen);

  FREE(&data);

  return ret;
}

int
mutt_hcache_store_raw (header_cache_t* h, const char* filename, void* data,
                       size_t dlen, size_t(*keylen) (const char* fn))
{
#ifndef HAVE_DB4
  BUFFER *path = NULL;
  int ksize;
  int rv = 0;
#endif
#if HAVE_GDBM
  datum key;
  datum databuf;
#elif HAVE_DB4
  DBT key;
  DBT databuf;
#elif HAVE_LMDB
  MDB_val key;
  MDB_val databuf;
#endif

  if (!h)
    return -1;

#if HAVE_DB4
  if (filename[0] == '/')
    filename++;

  mutt_hcache_dbt_init(&key, (void *) filename, keylen(filename));

  mutt_hcache_dbt_empty_init(&databuf);
  databuf.flags = DB_DBT_USERMEM;
  databuf.data = data;
  databuf.size = dlen;
  databuf.ulen = dlen;

  return h->db->put(h->db, NULL, &key, &databuf, 0);

#else
  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  ksize = strlen(h->folder) + keylen(filename);

#if HAVE_QDBM
  rv = vlput(h->db, mutt_b2s (path), ksize, data, dlen, VL_DOVER);
#elif HAVE_TC
  rv = tcbdbput(h->db, mutt_b2s (path), ksize, data, dlen);
#elif HAVE_KC
  rv = kcdbset(h->db, mutt_b2s (path), ksize, data, dlen);
#elif HAVE_GDBM
  key.dptr = path->data;
  key.dsize = ksize;

  databuf.dsize = dlen;
  databuf.dptr = data;

  rv = gdbm_store(h->db, key, databuf, GDBM_REPLACE);
#elif HAVE_LMDB
  key.mv_data = path->data;
  key.mv_size = ksize;
  databuf.mv_data = data;
  databuf.mv_size = dlen;
  if ((rv = mdb_get_w_txn (h)) == MDB_SUCCESS)
  {
    if ((rv = mdb_put (h->txn, h->db, &key, &databuf, 0)) != MDB_SUCCESS)
    {
      dprint (2, (debugfile, "mutt_hcache_store_raw: mdb_put: %s\n",
                  mdb_strerror(rv)));
      mdb_txn_abort (h->txn);
      h->txn_mode = txn_uninitialized;
      h->txn = NULL;
    }
  }
#endif

  mutt_buffer_pool_release (&path);
  return rv;
#endif
}

static char* get_foldername (const char *folder)
{
  char *p = NULL;
  BUFFER *path;
  struct stat st;

  path = mutt_buffer_pool_get ();
  mutt_encode_path (path, folder);

  /* if the folder is local, canonify the path to avoid
   * to ensure equivalent paths share the hcache */
  if (stat (mutt_b2s (path), &st) == 0)
  {
    p = safe_malloc (PATH_MAX+1);
    if (!realpath (mutt_b2s (path), p))
      mutt_str_replace (&p, mutt_b2s (path));
  }
  else
    p = safe_strdup (mutt_b2s (path));

  mutt_buffer_pool_release (&path);
  return p;
}

#if HAVE_QDBM
static int
hcache_open_qdbm (struct header_cache* h, const char* path)
{
  int    flags = VL_OWRITER | VL_OCREAT;

  if (option(OPTHCACHECOMPRESS))
    flags |= VL_OZCOMP;

  h->db = vlopen (path, flags, VL_CMPLEX);
  if (h->db)
    return 0;
  else
    return -1;
}

void
mutt_hcache_close(header_cache_t *h)
{
  if (!h)
    return;

  vlclose(h->db);
  FREE(&h->folder);
  FREE(&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  BUFFER *path = NULL;
  int ksize, rc;

  if (!h)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  ksize = strlen(h->folder) + keylen(filename);

  rc = vlout(h->db, mutt_b2s (path), ksize);

  mutt_buffer_pool_release (&path);
  return rc;
}

#elif HAVE_TC
static int
hcache_open_tc (struct header_cache* h, const char* path)
{
  h->db = tcbdbnew();
  if (!h->db)
    return -1;
  if (option(OPTHCACHECOMPRESS))
    tcbdbtune(h->db, 0, 0, 0, -1, -1, BDBTDEFLATE);
  if (tcbdbopen(h->db, path, BDBOWRITER | BDBOCREAT))
    return 0;
  else
  {
#ifdef DEBUG
    int ecode = tcbdbecode (h->db);
    dprint (2, (debugfile, "tcbdbopen failed for %s: %s (ecode %d)\n", path, tcbdberrmsg (ecode), ecode));
#endif
    tcbdbdel(h->db);
    return -1;
  }
}

void
mutt_hcache_close(header_cache_t *h)
{
  if (!h)
    return;

  if (!tcbdbclose(h->db))
  {
#ifdef DEBUG
    int ecode = tcbdbecode (h->db);
    dprint (2, (debugfile, "tcbdbclose failed for %s: %s (ecode %d)\n", h->folder, tcbdberrmsg (ecode), ecode));
#endif
  }
  tcbdbdel(h->db);
  FREE(&h->folder);
  FREE(&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  BUFFER *path = NULL;
  int ksize, rc;

  if (!h)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  ksize = strlen(h->folder) + keylen(filename);

  rc = tcbdbout(h->db, mutt_b2s (path), ksize);

  mutt_buffer_pool_release (&path);
  return rc;
}

#elif HAVE_KC
static int
hcache_open_kc (struct header_cache* h, const char* path)
{
  BUFFER *fullpath = NULL;
  int rc = -1;

  fullpath = mutt_buffer_pool_get ();

  /* Kyoto cabinet options are discussed at
   * http://fallabs.com/kyotocabinet/spex.html
   * - rcomp is by default lex, so there is no need to specify it.
   * - opts=l enables linear collision chaining as opposed to using a binary tree.
   *   this isn't suggested unless you are tuning the number of buckets.
   * - opts=c enables compression
   */
  mutt_buffer_printf (fullpath, "%s#type=kct%s", path,
                      option(OPTHCACHECOMPRESS) ? "#opts=c" : "");
  h->db = kcdbnew();
  if (!h->db)
    goto cleanup;
  if (!kcdbopen(h->db, mutt_b2s (fullpath), KCOWRITER | KCOCREATE))
  {
    dprint (2, (debugfile, "kcdbopen failed for %s: %s (ecode %d)\n",
                mutt_b2s (fullpath),
                kcdbemsg (h->db), kcdbecode (h->db)));
    kcdbdel(h->db);
    goto cleanup;
  }

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&fullpath);
  return rc;
}

void
mutt_hcache_close(header_cache_t *h)
{
  if (!h)
    return;

  if (!kcdbclose(h->db))
    dprint (2, (debugfile, "kcdbclose failed for %s: %s (ecode %d)\n", h->folder,
                kcdbemsg (h->db), kcdbecode (h->db)));
  kcdbdel(h->db);
  FREE(&h->folder);
  FREE(&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  BUFFER *path = NULL;
  int ksize, rc;

  if (!h)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  ksize = strlen(h->folder) + keylen(filename);

  rc = kcdbremove(h->db, mutt_b2s (path), ksize);

  mutt_buffer_pool_release (&path);
  return rc;
}

#elif HAVE_GDBM
static int
hcache_open_gdbm (struct header_cache* h, const char* path)
{
  int pagesize;

  pagesize = HeaderCachePageSize;
  if (pagesize <= 0)
    pagesize = 16384;

  h->db = gdbm_open((char *) path, pagesize, GDBM_WRCREAT, 00600, NULL);
  if (h->db)
    return 0;

  /* if rw failed try ro */
  h->db = gdbm_open((char *) path, pagesize, GDBM_READER, 00600, NULL);
  if (h->db)
    return 0;

  return -1;
}

void
mutt_hcache_close(header_cache_t *h)
{
  if (!h)
    return;

  gdbm_close(h->db);
  FREE(&h->folder);
  FREE(&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  datum key;
  BUFFER *path = NULL;
  int rc;

  if (!h)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);

  key.dptr = path->data;
  key.dsize = strlen(h->folder) + keylen(filename);

  rc = gdbm_delete(h->db, key);

  mutt_buffer_pool_release (&path);
  return rc;
}
#elif HAVE_DB4

static void
mutt_hcache_dbt_init(DBT * dbt, void *data, size_t len)
{
  dbt->data = data;
  dbt->size = dbt->ulen = len;
  dbt->dlen = dbt->doff = 0;
  dbt->flags = DB_DBT_USERMEM;
}

static void
mutt_hcache_dbt_empty_init(DBT * dbt)
{
  dbt->data = NULL;
  dbt->size = dbt->ulen = dbt->dlen = dbt->doff = 0;
  dbt->flags = 0;
}

static int
hcache_open_db4 (struct header_cache* h, const char* path)
{
  struct stat sb;
  int ret;
  uint32_t createflags = DB_CREATE;
  int pagesize;

  pagesize = HeaderCachePageSize;
  if (pagesize <= 0)
    pagesize = 16384;

  h->lockfile = mutt_buffer_new ();
  mutt_buffer_printf (h->lockfile, "%s-lock-hack", path);

  h->fd = open (mutt_b2s (h->lockfile), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (h->fd < 0)
    return -1;

  if (mx_lock_file (mutt_b2s (h->lockfile), h->fd, 1, 0, 5))
    goto fail_close;

  ret = db_env_create (&h->env, 0);
  if (ret)
    goto fail_unlock;

  ret = (*h->env->open)(h->env, NULL, DB_INIT_MPOOL | DB_CREATE | DB_PRIVATE,
                        0600);
  if (ret)
    goto fail_env;

  ret = db_create (&h->db, h->env, 0);
  if (ret)
    goto fail_env;

  if (stat(path, &sb) != 0 && errno == ENOENT)
  {
    createflags |= DB_EXCL;
    h->db->set_pagesize(h->db, pagesize);
  }

  ret = (*h->db->open)(h->db, NULL, path, h->folder, DB_BTREE, createflags,
                       0600);
  if (ret)
    goto fail_db;

  return 0;

fail_db:
  h->db->close (h->db, 0);
fail_env:
  h->env->close (h->env, 0);
fail_unlock:
  mx_unlock_file (mutt_b2s (h->lockfile), h->fd, 0);
fail_close:
  close (h->fd);
  unlink (mutt_b2s (h->lockfile));
  mutt_buffer_free (&h->lockfile);

  return -1;
}

void
mutt_hcache_close(header_cache_t *h)
{
  if (!h)
    return;

  h->db->close (h->db, 0);
  h->env->close (h->env, 0);
  mx_unlock_file (mutt_b2s (h->lockfile), h->fd, 0);
  close (h->fd);
  unlink (mutt_b2s (h->lockfile));
  mutt_buffer_free (&h->lockfile);
  FREE (&h->folder);
  FREE (&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  DBT key;

  if (!h)
    return -1;

  if (filename[0] == '/')
    filename++;

  mutt_hcache_dbt_init(&key, (void *) filename, keylen(filename));
  return h->db->del(h->db, NULL, &key, 0);
}
#elif HAVE_LMDB

static int
hcache_open_lmdb (struct header_cache* h, const char* path)
{
  int rc;

  h->txn = NULL;

  if ((rc = mdb_env_create(&h->env)) != MDB_SUCCESS)
  {
    dprint (2, (debugfile, "hcache_open_lmdb: mdb_env_create: %s\n",
                mdb_strerror(rc)));
    return -1;
  }

  mdb_env_set_mapsize(h->env, LMDB_DB_SIZE);

  if ((rc = mdb_env_open(h->env, path, MDB_NOSUBDIR, 0644)) != MDB_SUCCESS)
  {
    dprint (2, (debugfile, "hcache_open_lmdb: mdb_env_open: %s\n",
                mdb_strerror(rc)));
    goto fail_env;
  }

  if ((rc = mdb_get_r_txn(h)) != MDB_SUCCESS)
    goto fail_env;

  if ((rc = mdb_dbi_open(h->txn, NULL, MDB_CREATE, &h->db)) != MDB_SUCCESS)
  {
    dprint (2, (debugfile, "hcache_open_lmdb: mdb_dbi_open: %s\n",
                mdb_strerror(rc)));
    goto fail_dbi;
  }

  mdb_txn_reset(h->txn);
  h->txn_mode = txn_uninitialized;
  return 0;

fail_dbi:
  mdb_txn_abort(h->txn);
  h->txn_mode = txn_uninitialized;
  h->txn = NULL;

fail_env:
  mdb_env_close(h->env);
  return -1;
}

void
mutt_hcache_close(header_cache_t *h)
{
  int rc;

  if (!h)
    return;

  if (h->txn)
  {
    if (h->txn_mode == txn_write)
    {
      if ((rc = mdb_txn_commit (h->txn)) != MDB_SUCCESS)
      {
        dprint (2, (debugfile, "mutt_hcache_close: mdb_txn_commit: %s\n",
                    mdb_strerror (rc)));
      }
    }
    else
      mdb_txn_abort (h->txn);
    h->txn_mode = txn_uninitialized;
    h->txn = NULL;
  }

  mdb_env_close(h->env);
  FREE (&h->folder);
  FREE (&h);
}

int
mutt_hcache_delete(header_cache_t *h, const char *filename,
		   size_t(*keylen) (const char *fn))
{
  BUFFER *path = NULL;
  int ksize;
  MDB_val key;
  int rc = -1;

  if (!h)
    return -1;

  path = mutt_buffer_pool_get ();
  mutt_buffer_strcpy (path, h->folder);
  mutt_buffer_addstr (path, filename);
  ksize = strlen (h->folder) + keylen (filename);

  key.mv_data = path->data;
  key.mv_size = ksize;
  if (mdb_get_w_txn(h) != MDB_SUCCESS)
    goto cleanup;
  rc = mdb_del(h->txn, h->db, &key, NULL);
  if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND)
  {
    dprint (2, (debugfile, "mutt_hcache_delete: mdb_del: %s\n",
                mdb_strerror (rc)));
    mdb_txn_abort(h->txn);
    h->txn_mode = txn_uninitialized;
    h->txn = NULL;
    goto cleanup;
  }

  rc = 0;

cleanup:
  mutt_buffer_pool_release (&path);
  return rc;
}
#endif

header_cache_t *
mutt_hcache_open(const char *path, const char *folder, hcache_namer_t namer)
{
  struct header_cache *h = safe_calloc(1, sizeof (struct header_cache));
  int (*hcache_open) (struct header_cache* h, const char* path);
  struct stat sb;
  BUFFER *hcpath = NULL;

#if HAVE_QDBM
  hcache_open = hcache_open_qdbm;
#elif HAVE_TC
  hcache_open= hcache_open_tc;
#elif HAVE_KC
  hcache_open= hcache_open_kc;
#elif HAVE_GDBM
  hcache_open = hcache_open_gdbm;
#elif HAVE_DB4
  hcache_open = hcache_open_db4;
#elif HAVE_LMDB
  hcache_open = hcache_open_lmdb;
#endif

  /* Calculate the current hcache version from dynamic configuration */
  if (hcachever == 0x0)
  {
    union {
      unsigned char charval[16];
      unsigned int intval;
    } digest;
    struct md5_ctx ctx;
    REPLACE_LIST *spam;
    RX_LIST *nospam;

    hcachever = HCACHEVER;

    md5_init_ctx(&ctx);

    /* Seed with the compiled-in header structure hash */
    md5_process_bytes(&hcachever, sizeof(hcachever), &ctx);

    /* Mix in user's spam list */
    for (spam = SpamList; spam; spam = spam->next)
    {
      md5_process_bytes(spam->rx->pattern, strlen(spam->rx->pattern), &ctx);
      md5_process_bytes(spam->template, strlen(spam->template), &ctx);
    }

    /* Mix in user's nospam list */
    for (nospam = NoSpamList; nospam; nospam = nospam->next)
    {
      md5_process_bytes(nospam->rx->pattern, strlen(nospam->rx->pattern), &ctx);
    }

    /* Get a hash and take its bytes as an (unsigned int) hash version */
    md5_finish_ctx(&ctx, digest.charval);
    hcachever = digest.intval;
  }

#if HAVE_LMDB
  h->db = 0;
#else
  h->db = NULL;
#endif
  h->folder = get_foldername(folder);
  h->crc = hcachever;

  if (!path || path[0] == '\0')
  {
    FREE(&h->folder);
    FREE(&h);
    return NULL;
  }

  hcpath = mutt_buffer_pool_get ();
  mutt_hcache_per_folder(hcpath, path, h->folder, namer);

  if (hcache_open (h, mutt_b2s (hcpath)))
  {
    /* remove a possibly incompatible version */
    if (stat (mutt_b2s (hcpath), &sb) ||
        unlink (mutt_b2s (hcpath)) ||
        hcache_open (h, mutt_b2s (hcpath)))
    {
      FREE(&h->folder);
      FREE(&h);
    }
  }

  mutt_buffer_pool_release (&hcpath);
  return h;
}

void mutt_hcache_free (void **data)
{
  if (!data || !*data)
    return;

#if HAVE_KC
  kcfree (*data);
  *data = NULL;
#elif HAVE_LMDB
  /* LMDB owns the data returned.  It should not be freed */
#else
  FREE (data);  /* __FREE_CHECKED__ */
#endif
}

#if HAVE_DB4
const char *mutt_hcache_backend (void)
{
  return DB_VERSION_STRING;
}
#elif HAVE_LMDB
const char *mutt_hcache_backend (void)
{
  return "lmdb " MDB_VERSION_STRING;
}
#elif HAVE_GDBM
const char *mutt_hcache_backend (void)
{
  return gdbm_version;
}
#elif HAVE_QDBM
const char *mutt_hcache_backend (void)
{
  return "qdbm " _QDBM_VERSION;
}
#elif HAVE_TC
const char *mutt_hcache_backend (void)
{
  return "tokyocabinet " _TC_VERSION;
}
#elif HAVE_KC
const char *mutt_hcache_backend (void)
{
  static char backend[SHORT_STRING];
  snprintf(backend, sizeof(backend), "kyotocabinet %s", KCVERSION);
  return backend;
}
#endif
