/*
 * Copyright (C) 1996-2000,2007,2010 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2004,2006-2007 Thomas Roessler <roessler@does-not-exist.org>
 *
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General Public
 *     License as published by the Free Software Foundation; either
 *     version 2 of the License, or (at your option) any later
 *     version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *     Boston, MA  02110-1301, USA.
 */

/*
 * This file used to contain some more functions, namely those
 * which are now in muttlib.c.  They have been removed, so we have
 * some of our "standard" functions in external programs, too.
 */

#define _LIB_C 1

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef HAVE_SYSEXITS_H
#include <sysexits.h>
#else /* Make sure EX_OK is defined <philiph@pobox.com> */
#define EX_OK 0
#endif

#include "lib.h"


static const struct sysexits
{
  int v;
  const char *str;
}
sysexits_h[] =
{
#ifdef EX_USAGE
  { 0xff & EX_USAGE, "Bad usage." },
#endif
#ifdef EX_DATAERR
  { 0xff & EX_DATAERR, "Data format error." },
#endif
#ifdef EX_NOINPUT
  { 0xff & EX_NOINPUT, "Cannot open input." },
#endif
#ifdef EX_NOUSER
  { 0xff & EX_NOUSER, "User unknown." },
#endif
#ifdef EX_NOHOST
  { 0xff & EX_NOHOST, "Host unknown." },
#endif
#ifdef EX_UNAVAILABLE
  { 0xff & EX_UNAVAILABLE, "Service unavailable." },
#endif
#ifdef EX_SOFTWARE
  { 0xff & EX_SOFTWARE, "Internal error." },
#endif
#ifdef EX_OSERR
  { 0xff & EX_OSERR, "Operating system error." },
#endif
#ifdef EX_OSFILE
  { 0xff & EX_OSFILE, "System file missing." },
#endif
#ifdef EX_CANTCREAT
  { 0xff & EX_CANTCREAT, "Can't create output." },
#endif
#ifdef EX_IOERR
  { 0xff & EX_IOERR, "I/O error." },
#endif
#ifdef EX_TEMPFAIL
  { 0xff & EX_TEMPFAIL, "Deferred." },
#endif
#ifdef EX_PROTOCOL
  { 0xff & EX_PROTOCOL, "Remote protocol error." },
#endif
#ifdef EX_NOPERM
  { 0xff & EX_NOPERM, "Insufficient permission." },
#endif
#ifdef EX_CONFIG
  { 0xff & EX_NOPERM, "Local configuration error." },
#endif
  { S_ERR, "Exec error." },
  { -1, NULL}
};

void mutt_nocurses_error (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
}

void *safe_calloc (size_t nmemb, size_t size)
{
  void *p;

  if (!nmemb || !size)
    return NULL;

  if (((size_t) -1) / nmemb <= size)
  {
    mutt_error _("Integer overflow -- can't allocate memory!");
    sleep (1);
    mutt_exit (1);
  }

  if (!(p = calloc (nmemb, size)))
  {
    mutt_error _("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }
  return p;
}

void *safe_malloc (size_t siz)
{
  void *p;

  if (siz == 0)
    return 0;
  if ((p = (void *) malloc (siz)) == 0)	/* __MEM_CHECKED__ */
  {
    mutt_error _("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }
  return (p);
}

void safe_realloc (void *ptr, size_t siz)
{
  void *r;
  void **p = (void **)ptr;

  if (siz == 0)
  {
    if (*p)
    {
      free (*p);			/* __MEM_CHECKED__ */
      *p = NULL;
    }
    return;
  }

  if (*p)
    r = (void *) realloc (*p, siz);	/* __MEM_CHECKED__ */
  else
  {
    /* realloc(NULL, nbytes) doesn't seem to work under SunOS 4.1.x  --- __MEM_CHECKED__ */
    r = (void *) malloc (siz);		/* __MEM_CHECKED__ */
  }

  if (!r)
  {
    mutt_error _("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }

  *p = r;
}

void safe_free (void *ptr)	/* __SAFE_FREE_CHECKED__ */
{
  void **p = (void **)ptr;
  if (*p)
  {
    free (*p);				/* __MEM_CHECKED__ */
    *p = 0;
  }
}

int safe_fclose (FILE **f)
{
  int r = 0;

  if (*f)
    r = fclose (*f);

  *f = NULL;
  return r;
}

int safe_fsync_close (FILE **f)
{
  int r = 0;

  if (*f)
  {
    if (fflush (*f) || fsync (fileno (*f)))
    {
      r = -1;
      safe_fclose (f);
    }
    else
      r = safe_fclose (f);
  }

  return r;
}

char *safe_strdup (const char *s)
{
  char *p;
  size_t l;

  if (!s || !*s)
    return 0;
  l = strlen (s) + 1;
  p = (char *)safe_malloc (l);
  memcpy (p, s, l);
  return (p);
}

char *safe_strcat (char *d, size_t l, const char *s)
{
  char *p = d;

  if (!l)
    return d;

  l--; /* Space for the trailing '\0'. */

  for (; *d && l; l--)
    d++;
  for (; *s && l; l--)
    *d++ = *s++;

  *d = '\0';

  return p;
}

char *safe_strncat (char *d, size_t l, const char *s, size_t sl)
{
  char *p = d;

  if (!l)
    return d;

  l--; /* Space for the trailing '\0'. */

  for (; *d && l; l--)
    d++;
  for (; *s && l && sl; l--, sl--)
    *d++ = *s++;

  *d = '\0';

  return p;
}


/* Free *p afterwards to handle the case that *p and s reference the
 * same memory
 */
void mutt_str_replace (char **p, const char *s)
{
  char *tmp = *p;
  *p = safe_strdup (s);
  FREE (&tmp);
}

void mutt_str_adjust (char **p)
{
  if (!p || !*p) return;
  safe_realloc (p, strlen (*p) + 1);
}

/* convert all characters in the string to lowercase */
char *mutt_strlower (char *s)
{
  char *p = s;

  while (*p)
  {
    *p = tolower ((unsigned char) *p);
    p++;
  }

  return (s);
}

int mutt_mkdir (char *path, mode_t mode)
{
  struct stat sb;
  char *s;
  int rv = -1;

  if (stat (path, &sb) >= 0)
    return 0;

  s = path;
  do
  {
    s = strchr (s + 1, '/');
    if (s)
      *s = '\0';
    if (stat (path, &sb) < 0)
    {
      if (errno != ENOENT)
        goto cleanup;
      if (mkdir (path, mode) < 0)
        goto cleanup;
    }
    if (s)
      *s = '/';
  } while (s);

  rv = 0;

cleanup:
  if (s)
    *s = '/';

  return rv;
}

void mutt_unlink (const char *s)
{
  int fd;
  int flags;
  FILE *f;
  struct stat sb, sb2;
  char buf[2048];

  /* Defend against symlink attacks */

#ifdef O_NOFOLLOW
  flags = O_RDWR | O_NOFOLLOW;
#else
  flags = O_RDWR;
#endif

  if (lstat (s, &sb) == 0 && S_ISREG(sb.st_mode))
  {
    if ((fd = open (s, flags)) < 0)
      return;

    if ((fstat (fd, &sb2) != 0) || !S_ISREG (sb2.st_mode)
	|| (sb.st_dev != sb2.st_dev) || (sb.st_ino != sb2.st_ino))
    {
      close (fd);
      return;
    }

    if ((f = fdopen (fd, "r+")))
    {
      unlink (s);
      memset (buf, 0, sizeof (buf));
      while (sb.st_size > 0)
      {
	fwrite (buf, 1, MIN (sizeof (buf), sb.st_size), f);
	sb.st_size -= MIN (sizeof (buf), sb.st_size);
      }
      safe_fclose (&f);
    }
  }
}

int mutt_copy_bytes (FILE *in, FILE *out, size_t size)
{
  char buf[2048];
  size_t chunk;

  while (size > 0)
  {
    chunk = (size > sizeof (buf)) ? sizeof (buf) : size;
    if ((chunk = fread (buf, 1, chunk, in)) < 1)
      break;
    if (fwrite (buf, 1, chunk, out) != chunk)
    {
      /* dprint (1, (debugfile, "mutt_copy_bytes(): fwrite() returned short byte count\n")); */
      return (-1);
    }
    size -= chunk;
  }

  return 0;
}

int mutt_copy_stream (FILE *fin, FILE *fout)
{
  size_t l;
  char buf[LONG_STRING];

  while ((l = fread (buf, 1, sizeof (buf), fin)) > 0)
  {
    if (fwrite (buf, 1, l, fout) != l)
      return (-1);
  }

  return 0;
}

int
compare_stat (struct stat *osb, struct stat *nsb)
{
  if (osb->st_dev != nsb->st_dev || osb->st_ino != nsb->st_ino ||
      osb->st_rdev != nsb->st_rdev)
  {
    return -1;
  }

  return 0;
}



/*
 * This function is supposed to do nfs-safe renaming of files.
 *
 * Warning: We don't check whether src and target are equal.
 */

int safe_rename (const char *src, const char *target)
{
  struct stat ssb, tsb;
  int link_errno;

  if (!src || !target)
    return -1;

  if (link (src, target) != 0)
  {
    link_errno = errno;

    /*
     * It is historically documented that link can return -1 if NFS
     * dies after creating the link.  In that case, we are supposed
     * to use stat to check if the link was created.
     *
     * Derek Martin notes that some implementations of link() follow a
     * source symlink.  It might be more correct to use stat() on src.
     * I am not doing so to minimize changes in behavior: the function
     * used lstat() further below for 20 years without issue, and I
     * believe was never intended to be used on a src symlink.
     */
    if ((lstat (src, &ssb) == 0) &&
        (lstat (target, &tsb) == 0) &&
        (compare_stat (&ssb, &tsb) == 0))
    {
      dprint (1, (debugfile,
                  "safe_rename: link (%s, %s) reported failure: %s (%d) but actually succeded\n",
                  src, target, strerror (errno), errno));
      goto success;
    }

    errno = link_errno;

    /*
     * Coda does not allow cross-directory links, but tells
     * us it's a cross-filesystem linking attempt.
     *
     * However, the Coda rename call is allegedly safe to use.
     *
     * With other file systems, rename should just fail when
     * the files reside on different file systems, so it's safe
     * to try it here.
     *
     */

    dprint (1, (debugfile, "safe_rename: link (%s, %s) failed: %s (%d)\n", src, target, strerror (errno), errno));

    /*
     * FUSE may return ENOSYS. VFAT may return EPERM. FreeBSD's
     * msdosfs may return EOPNOTSUPP.  ENOTSUP can also appear.
     */
    if (errno == EXDEV || errno == ENOSYS || errno == EPERM
#ifdef ENOTSUP
	|| errno == ENOTSUP
#endif
#ifdef EOPNOTSUPP
	|| errno == EOPNOTSUPP
#endif
      )
    {
      dprint (1, (debugfile, "safe_rename: trying rename...\n"));
      if (rename (src, target) == -1)
      {
	dprint (1, (debugfile, "safe_rename: rename (%s, %s) failed: %s (%d)\n", src, target, strerror (errno), errno));
	return -1;
      }
      dprint (1, (debugfile, "safe_rename: rename succeeded.\n"));

      return 0;
    }

    return -1;
  }

  /*
   * Remove the compare_stat() check, because it causes problems with maildir on
   * filesystems that don't properly support hard links, such as
   * sshfs.  The filesystem creates the link, but the resulting file
   * is given a different inode number by the sshfs layer.  This
   * results in an infinite loop creating links.
   */
#if 0
  /*
   * Stat both links and check if they are equal.
   */
  if (lstat (src, &ssb) == -1)
  {
    dprint (1, (debugfile, "safe_rename: can't stat %s: %s (%d)\n",
		src, strerror (errno), errno));
    return -1;
  }

  if (lstat (target, &tsb) == -1)
  {
    dprint (1, (debugfile, "safe_rename: can't stat %s: %s (%d)\n",
		src, strerror (errno), errno));
    return -1;
  }

  /*
   * pretend that the link failed because the target file
   * did already exist.
   */
  if (compare_stat (&ssb, &tsb) == -1)
  {
    dprint (1, (debugfile, "safe_rename: stat blocks for %s and %s diverge; pretending EEXIST.\n", src, target));
    errno = EEXIST;
    return -1;
  }
#endif

success:
  /*
   * Unlink the original link.  Should we really ignore the return
   * value here? XXX
   */
  if (unlink (src) == -1)
  {
    dprint (1, (debugfile, "safe_rename: unlink (%s) failed: %s (%d)\n",
		src, strerror (errno), errno));
  }


  return 0;
}


static const char safe_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+@{}._-:%";

void mutt_sanitize_filename (char *f, int flags)
{
  int allow_slash, allow_8bit;

  if (!f) return;

  allow_slash = flags & MUTT_SANITIZE_ALLOW_SLASH;
  allow_8bit = flags & MUTT_SANITIZE_ALLOW_8BIT;

  for (; *f; f++)
  {
    if ((allow_slash && *f == '/')  ||
        (allow_8bit && (*f & 0x80)) ||
        strchr (safe_chars, *f))
      continue;
    else
      *f = '_';
  }
}

/* Read a line from ``fp'' into the dynamically allocated ``s'',
 * increasing ``s'' if necessary. The ending "\n" or "\r\n" is removed.
 * If a line ends with "\", this char and the linefeed is removed,
 * and the next line is read too.
 */
char *mutt_read_line (char *s, size_t *size, FILE *fp, int *line, int flags)
{
  size_t offset = 0;
  char *ch;

  if (!s)
  {
    s = safe_malloc (STRING);
    *size = STRING;
  }

  FOREVER
  {
    if (fgets (s + offset, *size - offset, fp) == NULL)
    {
      FREE (&s);
      return NULL;
    }
    if ((ch = strchr (s + offset, '\n')) != NULL)
    {
      if (line)
	(*line)++;
      if (flags & MUTT_EOL)
	return s;
      *ch = 0;
      if (ch > s && *(ch - 1) == '\r')
	*--ch = 0;
      if (!(flags & MUTT_CONT) || ch == s || *(ch - 1) != '\\')
	return s;
      offset = ch - s - 1;
    }
    else
    {
      int c;
      c = getc (fp); /* This is kind of a hack. We want to know if the
                        char at the current point in the input stream is EOF.
                        feof() will only tell us if we've already hit EOF, not
                        if the next character is EOF. So, we need to read in
                        the next character and manually check if it is EOF. */
      if (c == EOF)
      {
        /* The last line of fp isn't \n terminated */
	if (line)
	  (*line)++;
        return s;
      }
      else
      {
        ungetc (c, fp); /* undo our damage */
        /* There wasn't room for the line -- increase ``s'' */
        offset = *size - 1; /* overwrite the terminating 0 */
        *size += STRING;
        safe_realloc (&s, *size);
      }
    }
  }
}

char *
mutt_substrcpy (char *dest, const char *beg, const char *end, size_t destlen)
{
  size_t len;

  len = end - beg;
  if (len > destlen - 1)
    len = destlen - 1;
  memcpy (dest, beg, len);
  dest[len] = 0;
  return dest;
}

char *mutt_substrdup (const char *begin, const char *end)
{
  size_t len;
  char *p;

  if (end)
    len = end - begin;
  else
    len = strlen (begin);

  p = safe_malloc (len + 1);
  memcpy (p, begin, len);
  p[len] = 0;
  return p;
}

/* NULL-pointer aware string comparison functions */

int mutt_strcmp(const char *a, const char *b)
{
  return strcmp(NONULL(a), NONULL(b));
}

int mutt_strcasecmp(const char *a, const char *b)
{
  return strcasecmp(NONULL(a), NONULL(b));
}

int mutt_strncmp(const char *a, const char *b, size_t l)
{
  return strncmp(NONULL(a), NONULL(b), l);
}

int mutt_strncasecmp(const char *a, const char *b, size_t l)
{
  return strncasecmp(NONULL(a), NONULL(b), l);
}

size_t mutt_strlen(const char *a)
{
  return a ? strlen (a) : 0;
}

int mutt_strcoll(const char *a, const char *b)
{
  return strcoll(NONULL(a), NONULL(b));
}

const char *mutt_stristr (const char *haystack, const char *needle)
{
  const char *p, *q;

  if (!haystack)
    return NULL;
  if (!needle)
    return (haystack);

  while (*(p = haystack))
  {
    for (q = needle;
         *p && *q &&
           tolower ((unsigned char) *p) == tolower ((unsigned char) *q);
         p++, q++)
      ;
    if (!*q)
      return (haystack);
    haystack++;
  }
  return NULL;
}

char *mutt_skip_whitespace (char *p)
{
  SKIPWS (p);
  return p;
}

void mutt_remove_trailing_ws (char *s)
{
  char *p;

  for (p = s + mutt_strlen (s) - 1 ; p >= s && ISSPACE (*p) ; p--)
    *p = 0;
}

char *mutt_concat_path (char *d, const char *dir, const char *fname, size_t l)
{
  const char *fmt = "%s/%s";

  if (!*fname || (*dir && dir[strlen(dir)-1] == '/'))
    fmt = "%s%s";

  snprintf (d, l, fmt, dir, fname);
  return d;
}

const char *mutt_basename (const char *f)
{
  const char *p = strrchr (f, '/');
  if (p)
    return p + 1;
  else
    return f;
}

const char *
mutt_strsysexit(int e)
{
  int i;

  for (i = 0; sysexits_h[i].str; i++)
  {
    if (e == sysexits_h[i].v)
      break;
  }

  return sysexits_h[i].str;
}

#ifdef DEBUG

void mutt_debug (FILE *fp, const char *fmt, ...)
{
  va_list ap;
  time_t now = time (NULL);
  static char buf[23] = "";
  static time_t last = 0;

  if (now > last)
  {
    strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S", localtime (&now));
    last = now;
  }
  fprintf (fp, "[%s] ", buf);
  va_start (ap, fmt);
  vfprintf (fp, fmt, ap);
  va_end (ap);
}

void mutt_debug_f (const char *file, const int line, const char *function, const char *fmt, ...)
{
  va_list ap;
  time_t now = time (NULL);
  static char buf[23] = "";
  static time_t last = 0;

  if (now > last)
  {
    strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S", localtime (&now));
    last = now;
  }

  if (function)
    fprintf (debugfile, "[%s %s@%s:%d] ", buf, function, file, line);
  else
    fprintf (debugfile, "[%s %s:%d] ", buf, file, line);

  va_start (ap, fmt);
  vfprintf (debugfile, fmt, ap);
  va_end (ap);

  /* because we always print a line header, in dprintf() we auto-newline */
  if (strchr(fmt, '\n') == NULL)
    fputc('\n', debugfile);
}

#endif /* DEBUG */


/**********************************************************************
 * mutt_atoX functions
 *
 * By default these all operate in a "strict mode", returning:
 *
 * * -1 if input is NULL or "".
 *   Pass flag MUTT_ATOI_ALLOW_EMPTY to return 0 in that case.
 *
 * * -1 if there is trailing input after the number.
 *   Pass flag MUTT_ATOI_ALLOW_TRAILING to return 0 in that case.
 *
 * * -2 if the number is out of range
 *
 * Note that the dst parameter will be set to 0 on error.
 *********************************************************************/

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atos (const char *str, short *dst, int flags)
{
  int rc;
  long res;
  short tmp;
  short *t = dst ? dst : &tmp;

  *t = 0;

  if ((rc = mutt_atol (str, &res, flags)) < 0)
    return rc;
  if ((short) res != res)
    return -2;

  *t = (short) res;
  return rc;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atoi (const char *str, int *dst, int flags)
{
  int rc;
  long res;
  int tmp;
  int *t = dst ? dst : &tmp;

  *t = 0;

  if ((rc = mutt_atol (str, &res, flags)) < 0)
    return rc;
  if ((int) res != res)
    return -2;

  *t = (int) res;
  return rc;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atol (const char *str, long *dst, int flags)
{
  long tmp, res;
  long *t = dst ? dst : &tmp;
  char *e = NULL;

  *t = 0;

  if (!str || !*str)
    return (flags & MUTT_ATOI_ALLOW_EMPTY) ? 0 : -1;

  errno = 0;
  res = strtol (str, &e, 10);

  if (errno == ERANGE)
    return -2;
  if (e == str)
    return -1;
  if ((*e != '\0') && !(flags & MUTT_ATOI_ALLOW_TRAILING))
    return -1;

  *t = res;
  return 0;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atoll (const char *str, long long *dst, int flags)
{
  long long tmp, res;
  long long *t = dst ? dst : &tmp;
  char *e = NULL;

  *t = 0;

  if (!str || !*str)
    return (flags & MUTT_ATOI_ALLOW_EMPTY) ? 0 : -1;

  errno = 0;
  res = strtoll (str, &e, 10);

  if (errno == ERANGE)
    return -2;
  if (e == str)
    return -1;
  if ((*e != '\0') && !(flags & MUTT_ATOI_ALLOW_TRAILING))
    return -1;

  *t = res;
  return 0;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atoui (const char *str, unsigned int *dst, int flags)
{
  int rc;
  unsigned long res;
  unsigned int tmp;
  unsigned int *t = dst ? dst : &tmp;

  *t = 0;

  if ((rc = mutt_atoul (str, &res, flags)) < 0)
    return rc;
  if ((unsigned int) res != res)
    return -2;

  *t = (unsigned int) res;
  return rc;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atoul (const char *str, unsigned long *dst, int flags)
{
  unsigned long tmp, res;
  unsigned long *t = dst ? dst : &tmp;
  char *e = NULL;

  *t = 0;

  if (!str || !*str)
    return (flags & MUTT_ATOI_ALLOW_EMPTY) ? 0 : -1;

  errno = 0;
  res = strtoul (str, &e, 10);

  if (errno == ERANGE)
    return -2;
  if (e == str)
    return -1;
  if ((*e != '\0') && !(flags & MUTT_ATOI_ALLOW_TRAILING))
    return -1;

  *t = res;
  return 0;
}

/* returns: 0 - successful conversion
 *         -1 - error: invalid input
 *         -2 - error: out of range
 */
int mutt_atoull (const char *str, unsigned long long *dst, int flags)
{
  unsigned long long tmp, res;
  unsigned long long *t = dst ? dst : &tmp;
  char *e = NULL;

  *t = 0;

  if (!str || !*str)
    return (flags & MUTT_ATOI_ALLOW_EMPTY) ? 0 : -1;

  errno = 0;
  res = strtoull (str, &e, 10);

  if (errno == ERANGE)
    return -2;
  if (e == str)
    return -1;
  if ((*e != '\0') && !(flags & MUTT_ATOI_ALLOW_TRAILING))
    return -1;

  *t = res;
  return 0;
}
