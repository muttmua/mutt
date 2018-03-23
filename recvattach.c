/*
 * Copyright (C) 1996-2000,2002,2007,2010 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2006 Thomas Roessler <roessler@does-not-exist.org>
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
#include "rfc1524.h"
#include "mime.h"
#include "mailbox.h"
#include "attach.h"
#include "mapping.h"
#include "mx.h"
#include "mutt_crypt.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

static void mutt_update_recvattach_menu (ATTACH_CONTEXT *actx, MUTTMENU *menu, int init);

static const char *Mailbox_is_read_only = N_("Mailbox is read-only.");

#define CHECK_READONLY if (Context->readonly) \
{\
    mutt_flushinp (); \
    mutt_error _(Mailbox_is_read_only); \
    break; \
}

#define CURATTACH actx->idx[actx->v2r[menu->current]]

static const struct mapping_t AttachHelp[] = {
  { N_("Exit"),  OP_EXIT },
  { N_("Save"),  OP_SAVE },
  { N_("Pipe"),  OP_PIPE },
  { N_("Print"), OP_PRINT },
  { N_("Help"),  OP_HELP },
  { NULL,        0 }
};

static void mutt_update_v2r (ATTACH_CONTEXT *actx)
{
  int vindex, rindex, curlevel;

  vindex = rindex = 0;

  while (rindex < actx->idxlen)
  {
    actx->v2r[vindex++] = rindex;
    if (actx->idx[rindex]->content->collapsed)
    {
      curlevel = actx->idx[rindex]->level;
      do
        rindex++;
      while ((rindex < actx->idxlen) &&
             (actx->idx[rindex]->level > curlevel));
    }
    else
      rindex++;
  }

  actx->vcount = vindex;
}

void mutt_update_tree (ATTACH_CONTEXT *actx)
{
  char buf[STRING];
  char *s;
  int rindex, vindex;

  mutt_update_v2r (actx);

  for (vindex = 0; vindex < actx->vcount; vindex++)
  {
    rindex = actx->v2r[vindex];
    actx->idx[rindex]->num = vindex;
    if (2 * (actx->idx[rindex]->level + 2) < sizeof (buf))
    {
      if (actx->idx[rindex]->level)
      {
	s = buf + 2 * (actx->idx[rindex]->level - 1);
	*s++ = (actx->idx[rindex]->content->next) ? MUTT_TREE_LTEE : MUTT_TREE_LLCORNER;
	*s++ = MUTT_TREE_HLINE;
	*s++ = MUTT_TREE_RARROW;
      }
      else
	s = buf;
      *s = 0;
    }

    if (actx->idx[rindex]->tree)
    {
      if (mutt_strcmp (actx->idx[rindex]->tree, buf) != 0)
	mutt_str_replace (&actx->idx[rindex]->tree, buf);
    }
    else
      actx->idx[rindex]->tree = safe_strdup (buf);

    if (2 * (actx->idx[rindex]->level + 2) < sizeof (buf) && actx->idx[rindex]->level)
    {
      s = buf + 2 * (actx->idx[rindex]->level - 1);
      *s++ = (actx->idx[rindex]->content->next) ? '\005' : '\006';
      *s++ = '\006';
    }
  }
}

/* %c = character set: convert?
 * %C = character set
 * %D = deleted flag
 * %d = description
 * %e = MIME content-transfer-encoding
 * %F = filename for content-disposition header
 * %f = filename
 * %I = content-disposition, either I (inline) or A (attachment)
 * %t = tagged flag
 * %T = tree chars
 * %m = major MIME type
 * %M = MIME subtype
 * %n = attachment number
 * %s = size
 * %u = unlink 
 */
const char *mutt_attach_fmt (char *dest,
    size_t destlen,
    size_t col,
    int cols,
    char op,
    const char *src,
    const char *prefix,
    const char *ifstring,
    const char *elsestring,
    unsigned long data,
    format_flag flags)
{
  char fmt[16];
  char tmp[SHORT_STRING];
  char charset[SHORT_STRING];
  ATTACHPTR *aptr = (ATTACHPTR *) data;
  int optional = (flags & MUTT_FORMAT_OPTIONAL);
  size_t l;
  
  switch (op)
  {
    case 'C':
      if (!optional)
      {
	if (mutt_is_text_part (aptr->content) &&
	    mutt_get_body_charset (charset, sizeof (charset), aptr->content))
	  mutt_format_s (dest, destlen, prefix, charset);
	else
	  mutt_format_s (dest, destlen, prefix, "");
      }
      else if (!mutt_is_text_part (aptr->content) ||
	       !mutt_get_body_charset (charset, sizeof (charset), aptr->content))
        optional = 0;
      break;
    case 'c':
      /* XXX */
      if (!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%sc", prefix);
	snprintf (dest, destlen, fmt, aptr->content->type != TYPETEXT ||
		  aptr->content->noconv ? 'n' : 'c');
      }
      else if (aptr->content->type != TYPETEXT || aptr->content->noconv)
        optional = 0;
      break;
    case 'd':
      if(!optional)
      {
	if (aptr->content->description)
	{
	  mutt_format_s (dest, destlen, prefix, aptr->content->description);
	  break;
	}
	if (mutt_is_message_type(aptr->content->type, aptr->content->subtype) &&
	    MsgFmt && aptr->content->hdr)
	{
	  char s[SHORT_STRING];
	  _mutt_make_string (s, sizeof (s), MsgFmt, NULL, aptr->content->hdr,
			     MUTT_FORMAT_FORCESUBJ | MUTT_FORMAT_MAKEPRINT | MUTT_FORMAT_ARROWCURSOR);
	  if (*s)
	  {
	    mutt_format_s (dest, destlen, prefix, s);
	    break;
	  }
	}
        if (!aptr->content->d_filename && !aptr->content->filename)
	{
	  mutt_format_s (dest, destlen, prefix, "<no description>");
	  break;
	}
      }
      else if(aptr->content->description || 
	      (mutt_is_message_type (aptr->content->type, aptr->content->subtype)
	      && MsgFmt && aptr->content->hdr))
        break;
    /* FALLS THROUGH TO 'F' */
    case 'F':
      if (!optional)
      {
        if (aptr->content->d_filename)
        {
          mutt_format_s (dest, destlen, prefix, aptr->content->d_filename);
          break;
        }
      }
      else if (!aptr->content->d_filename && !aptr->content->filename)
      {
        optional = 0;
        break;
      }
    /* FALLS THROUGH TO 'f' */
    case 'f':
      if(!optional)
      {
	if (aptr->content->filename && *aptr->content->filename == '/')
	{
	  char path[_POSIX_PATH_MAX];
	  
	  strfcpy (path, aptr->content->filename, sizeof (path));
	  mutt_pretty_mailbox (path, sizeof (path));
	  mutt_format_s (dest, destlen, prefix, path);
	}
	else
	  mutt_format_s (dest, destlen, prefix, NONULL (aptr->content->filename));
      }
      else if(!aptr->content->filename)
        optional = 0;
      break;
    case 'D':
      if(!optional)
	snprintf (dest, destlen, "%c", aptr->content->deleted ? 'D' : ' ');
      else if(!aptr->content->deleted)
        optional = 0;
      break;
    case 'e':
      if(!optional)
	mutt_format_s (dest, destlen, prefix,
		      ENCODING (aptr->content->encoding));
      break;
    case 'I':
      if (!optional)
      {
	const char dispchar[] = { 'I', 'A', 'F', '-' };
	char ch;

	if (aptr->content->disposition < sizeof(dispchar))
	  ch = dispchar[aptr->content->disposition];
	else
	{
	  dprint(1, (debugfile, "ERROR: invalid content-disposition %d\n", aptr->content->disposition));
	  ch = '!';
	}
	snprintf (dest, destlen, "%c", ch);
      }
      break;
    case 'm':
      if(!optional)
	mutt_format_s (dest, destlen, prefix, TYPE (aptr->content));
      break;
    case 'M':
      if(!optional)
	mutt_format_s (dest, destlen, prefix, aptr->content->subtype);
      else if(!aptr->content->subtype)
        optional = 0;
      break;
    case 'n':
      if(!optional)
      {
	snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	snprintf (dest, destlen, fmt, aptr->num + 1);
      }
      break;
    case 'Q':
      if (optional)
        optional = aptr->content->attach_qualifies;
      else {
	    snprintf (fmt, sizeof (fmt), "%%%sc", prefix);
        mutt_format_s (dest, destlen, fmt, "Q");
      }
      break;
    case 's':
      if (flags & MUTT_FORMAT_STAT_FILE)
      {
	struct stat st;
	stat (aptr->content->filename, &st);
	l = st.st_size;
      }
      else
        l = aptr->content->length;
      
      if(!optional)
      {
	mutt_pretty_size (tmp, sizeof(tmp), l);
	mutt_format_s (dest, destlen, prefix, tmp);
      }
      else if (l == 0)
        optional = 0;

      break;
    case 't':
      if(!optional)
        snprintf (dest, destlen, "%c", aptr->content->tagged ? '*' : ' ');
      else if(!aptr->content->tagged)
        optional = 0;
      break;
    case 'T':
      if(!optional)
	mutt_format_s_tree (dest, destlen, prefix, NONULL (aptr->tree));
      else if (!aptr->tree)
        optional = 0;
      break;
    case 'u':
      if(!optional)
        snprintf (dest, destlen, "%c", aptr->content->unlink ? '-' : ' ');
      else if (!aptr->content->unlink)
        optional = 0;
      break;
    case 'X':
      if (optional)
        optional = (aptr->content->attach_count + aptr->content->attach_qualifies) != 0;
      else
      {
        snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
        snprintf (dest, destlen, fmt, aptr->content->attach_count + aptr->content->attach_qualifies);
      }
      break;
    default:
      *dest = 0;
  }
  
  if (optional)
    mutt_FormatString (dest, destlen, col, cols, ifstring, mutt_attach_fmt, data, 0);
  else if (flags & MUTT_FORMAT_OPTIONAL)
    mutt_FormatString (dest, destlen, col, cols, elsestring, mutt_attach_fmt, data, 0);
  return (src);
}

static void attach_entry (char *b, size_t blen, MUTTMENU *menu, int num)
{
  ATTACH_CONTEXT *actx = (ATTACH_CONTEXT *)menu->data;

  mutt_FormatString (b, blen, 0, MuttIndexWindow->cols, NONULL (AttachFormat), mutt_attach_fmt,
                     (unsigned long) (actx->idx[actx->v2r[num]]), MUTT_FORMAT_ARROWCURSOR);
}

int mutt_tag_attach (MUTTMENU *menu, int n, int m)
{
  ATTACH_CONTEXT *actx = (ATTACH_CONTEXT *)menu->data;
  BODY *cur = actx->idx[actx->v2r[n]]->content;
  int ot = cur->tagged;

  cur->tagged = (m >= 0 ? m : !cur->tagged);
  return cur->tagged - ot;
}

int mutt_is_message_type (int type, const char *subtype)
{
  if (type != TYPEMESSAGE)
    return 0;

  subtype = NONULL(subtype);
  return (ascii_strcasecmp (subtype, "rfc822") == 0 || ascii_strcasecmp (subtype, "news") == 0);
}

static void prepend_curdir (char *dst, size_t dstlen)
{
  size_t l;

  if (!dst || !*dst || *dst == '/' || dstlen < 3 ||
      /* XXX bad modularization, these are special to mutt_expand_path() */
      !strchr ("~=+@<>!-^", *dst))
    return;

  dstlen -= 3;
  l = strlen (dst) + 2;
  l = (l > dstlen ? dstlen : l);
  memmove (dst + 2, dst, l);
  dst[0] = '.';
  dst[1] = '/';
  dst[l + 2] = 0;
}

static int mutt_query_save_attachment (FILE *fp, BODY *body, HEADER *hdr, char **directory)
{
  char *prompt;
  char buf[_POSIX_PATH_MAX], tfile[_POSIX_PATH_MAX];
  int is_message;
  int append = 0;
  int rc;
  
  if (body->filename) 
  {
    if (directory && *directory)
      mutt_concat_path (buf, *directory, mutt_basename (body->filename), sizeof (buf));
    else
      strfcpy (buf, body->filename, sizeof (buf));
  }
  else if(body->hdr &&
	  body->encoding != ENCBASE64 &&
	  body->encoding != ENCQUOTEDPRINTABLE &&
	  mutt_is_message_type(body->type, body->subtype))
    mutt_default_save(buf, sizeof(buf), body->hdr);
  else
    buf[0] = 0;

  prepend_curdir (buf, sizeof (buf));

  prompt = _("Save to file: ");
  while (prompt)
  {
    if (mutt_get_field (prompt, buf, sizeof (buf), MUTT_FILE | MUTT_CLEAR) != 0
	|| !buf[0])
    {
      mutt_clear_error ();
      return -1;
    }
    
    prompt = NULL;
    mutt_expand_path (buf, sizeof (buf));
    
    is_message = (fp && 
		  body->hdr && 
		  body->encoding != ENCBASE64 && 
		  body->encoding != ENCQUOTEDPRINTABLE && 
		  mutt_is_message_type (body->type, body->subtype));
    
    if (is_message)
    {
      struct stat st;
      
      /* check to make sure that this file is really the one the user wants */
      if ((rc = mutt_save_confirm (buf, &st)) == 1)
      {
	prompt = _("Save to file: ");
	continue;
      } 
      else if (rc == -1)
	return -1;
      strfcpy(tfile, buf, sizeof(tfile));
    }
    else
    {
      if ((rc = mutt_check_overwrite (body->filename, buf, tfile, sizeof (tfile), &append, directory)) == -1)
	return -1;
      else if (rc == 1)
      {
	prompt = _("Save to file: ");
	continue;
      }
    }
    
    mutt_message _("Saving...");
    if (mutt_save_attachment (fp, body, tfile, append, (hdr || !is_message) ? hdr : body->hdr) == 0)
    {
      mutt_message _("Attachment saved.");
      return 0;
    }
    else
    {
      prompt = _("Save to file: ");
      continue;
    }
  }
  return 0;
}
    
void mutt_save_attachment_list (ATTACH_CONTEXT *actx, FILE *fp, int tag, BODY *top, HEADER *hdr, MUTTMENU *menu)
{
  char buf[_POSIX_PATH_MAX], tfile[_POSIX_PATH_MAX];
  char *directory = NULL;
  int i, rc = 1;
  int last = menu ? menu->current : -1;
  FILE *fpout;

  buf[0] = 0;

  for (i = 0; !tag || i < actx->idxlen; i++)
  {
    if (tag)
    {
      fp = actx->idx[i]->fp;
      top = actx->idx[i]->content;
    }
    if (!tag || top->tagged)
    {
      if (!option (OPTATTACHSPLIT))
      {
	if (!buf[0])
	{
	  int append = 0;

	  strfcpy (buf, mutt_basename (NONULL (top->filename)), sizeof (buf));
	  prepend_curdir (buf, sizeof (buf));

	  if (mutt_get_field (_("Save to file: "), buf, sizeof (buf),
				    MUTT_FILE | MUTT_CLEAR) != 0 || !buf[0])
	    return;
	  mutt_expand_path (buf, sizeof (buf));
	  if (mutt_check_overwrite (top->filename, buf, tfile,
				    sizeof (tfile), &append, NULL))
	    return;
	  rc = mutt_save_attachment (fp, top, tfile, append, hdr);
	  if (rc == 0 && AttachSep && (fpout = fopen (tfile,"a")) != NULL)
	  {
	    fprintf(fpout, "%s", AttachSep);
	    safe_fclose (&fpout);
	  }
	}
	else
	{
	  rc = mutt_save_attachment (fp, top, tfile, MUTT_SAVE_APPEND, hdr);
	  if (rc == 0 && AttachSep && (fpout = fopen (tfile,"a")) != NULL)
	  {
	    fprintf(fpout, "%s", AttachSep);
	    safe_fclose (&fpout);
	  }
	}
      }
      else 
      {
	if (tag && menu && top->aptr)
	{
	  menu->oldcurrent = menu->current;
	  menu->current = top->aptr->num;
	  menu_check_recenter (menu);
	  menu->redraw |= REDRAW_MOTION;

	  menu_redraw (menu);
	}
	if (mutt_query_save_attachment (fp, top, hdr, &directory) == -1)
	  break;
      }
    }
    if (!tag)
      break;
  }

  FREE (&directory);

  if (tag && menu)
  {
    menu->oldcurrent = menu->current;
    menu->current = last;
    menu_check_recenter (menu);
    menu->redraw |= REDRAW_MOTION;
  }
  
  if (!option (OPTATTACHSPLIT) && (rc == 0))
    mutt_message _("Attachment saved.");
}

static void
mutt_query_pipe_attachment (char *command, FILE *fp, BODY *body, int filter)
{
  char tfile[_POSIX_PATH_MAX];
  char warning[STRING+_POSIX_PATH_MAX];

  if (filter)
  {
    snprintf (warning, sizeof (warning),
	      _("WARNING!  You are about to overwrite %s, continue?"),
	      body->filename);
    if (mutt_yesorno (warning, MUTT_NO) != MUTT_YES) {
      mutt_window_clearline (MuttMessageWindow, 0);
      return;
    }
    mutt_mktemp (tfile, sizeof (tfile));
  }
  else
    tfile[0] = 0;

  if (mutt_pipe_attachment (fp, body, command, tfile))
  {
    if (filter)
    {
      mutt_unlink (body->filename);
      mutt_rename_file (tfile, body->filename);
      mutt_update_encoding (body);
      mutt_message _("Attachment filtered.");
    }
  }
  else
  {
    if (filter && tfile[0])
      mutt_unlink (tfile);
  }
}

static void pipe_attachment (FILE *fp, BODY *b, STATE *state)
{
  FILE *ifp;

  if (fp)
  {
    state->fpin = fp;
    mutt_decode_attachment (b, state);
    if (AttachSep)
      state_puts (AttachSep, state);
  }
  else
  {
    if ((ifp = fopen (b->filename, "r")) == NULL)
    {
      mutt_perror ("fopen");
      return;
    }
    mutt_copy_stream (ifp, state->fpout);
    safe_fclose (&ifp);
    if (AttachSep)
      state_puts (AttachSep, state);
  }
}

static void
pipe_attachment_list (char *command, ATTACH_CONTEXT *actx, FILE *fp, int tag,
                      BODY *top, int filter, STATE *state)
{
  int i;

  for (i = 0; !tag || i < actx->idxlen; i++)
  {
    if (tag)
    {
      fp = actx->idx[i]->fp;
      top = actx->idx[i]->content;
    }
    if (!tag || top->tagged)
    {
      if (!filter && !option (OPTATTACHSPLIT))
	pipe_attachment (fp, top, state);
      else
	mutt_query_pipe_attachment (command, fp, top, filter);
    }
    if (!tag)
      break;
  }
}

void mutt_pipe_attachment_list (ATTACH_CONTEXT *actx, FILE *fp, int tag, BODY *top, int filter)
{
  STATE state;
  char buf[SHORT_STRING];
  pid_t thepid;

  if (fp)
    filter = 0; /* sanity check: we can't filter in the recv case yet */

  buf[0] = 0;
  memset (&state, 0, sizeof (STATE));
  /* perform charset conversion on text attachments when piping */
  state.flags = MUTT_CHARCONV;

  if (mutt_get_field ((filter ? _("Filter through: ") : _("Pipe to: ")),
				  buf, sizeof (buf), MUTT_CMD) != 0 || !buf[0])
    return;

  mutt_expand_path (buf, sizeof (buf));

  if (!filter && !option (OPTATTACHSPLIT))
  {
    mutt_endwin (NULL);
    thepid = mutt_create_filter (buf, &state.fpout, NULL, NULL);
    pipe_attachment_list (buf, actx, fp, tag, top, filter, &state);
    safe_fclose (&state.fpout);
    if (mutt_wait_filter (thepid) != 0 || option (OPTWAITKEY))
      mutt_any_key_to_continue (NULL);
  }
  else
    pipe_attachment_list (buf, actx, fp, tag, top, filter, &state);
}

static int can_print (ATTACH_CONTEXT *actx, BODY *top, int tag)
{
  char type [STRING];
  int i;

  for (i = 0; !tag || i < actx->idxlen; i++)
  {
    if (tag)
      top = actx->idx[i]->content;
    snprintf (type, sizeof (type), "%s/%s", TYPE (top), top->subtype);
    if (!tag || top->tagged)
    {
      if (!rfc1524_mailcap_lookup (top, type, NULL, MUTT_PRINT))
      {
	if (ascii_strcasecmp ("text/plain", top->subtype) &&
	    ascii_strcasecmp ("application/postscript", top->subtype))
	{
	  if (!mutt_can_decode (top))
	  {
	    mutt_error (_("I don't know how to print %s attachments!"), type);
	    return (0);
	  }
	}
      }
    }
    if (!tag)
      break;
  }
  return (1);
}

static void print_attachment_list (ATTACH_CONTEXT *actx, FILE *fp, int tag, BODY *top, STATE *state)
{
  int i;
  char type [STRING];

  for (i = 0; !tag || i < actx->idxlen; i++)
  {
    if (tag)
    {
      fp = actx->idx[i]->fp;
      top = actx->idx[i]->content;
    }
    if (!tag || top->tagged)
    {
      snprintf (type, sizeof (type), "%s/%s", TYPE (top), top->subtype);
      if (!option (OPTATTACHSPLIT) && !rfc1524_mailcap_lookup (top, type, NULL, MUTT_PRINT))
      {
	if (!ascii_strcasecmp ("text/plain", top->subtype) ||
	    !ascii_strcasecmp ("application/postscript", top->subtype))
	  pipe_attachment (fp, top, state);
	else if (mutt_can_decode (top))
	{
	  /* decode and print */

	  char newfile[_POSIX_PATH_MAX] = "";
	  FILE *ifp;

	  mutt_mktemp (newfile, sizeof (newfile));
	  if (mutt_decode_save_attachment (fp, top, newfile, MUTT_PRINTING, 0) == 0)
	  {
	    if ((ifp = fopen (newfile, "r")) != NULL)
	    {
	      mutt_copy_stream (ifp, state->fpout);
	      safe_fclose (&ifp);
	      if (AttachSep)
		state_puts (AttachSep, state);
	    }
	  }
	  mutt_unlink (newfile);
	}
      }
      else
	mutt_print_attachment (fp, top);
    }
    if (!tag)
      break;
  }
}

void mutt_print_attachment_list (ATTACH_CONTEXT *actx, FILE *fp, int tag, BODY *top)
{
  STATE state;
  
  pid_t thepid;
  if (query_quadoption (OPT_PRINT, tag ? _("Print tagged attachment(s)?") : _("Print attachment?")) != MUTT_YES)
    return;

  if (!option (OPTATTACHSPLIT))
  {
    if (!can_print (actx, top, tag))
      return;
    mutt_endwin (NULL);
    memset (&state, 0, sizeof (STATE));
    thepid = mutt_create_filter (NONULL (PrintCmd), &state.fpout, NULL, NULL);
    print_attachment_list (actx, fp, tag, top, &state);
    safe_fclose (&state.fpout);
    if (mutt_wait_filter (thepid) != 0 || option (OPTWAITKEY))
      mutt_any_key_to_continue (NULL);
  }
  else
    print_attachment_list (actx, fp, tag, top, &state);
}

static void recvattach_extract_pgp_keys (ATTACH_CONTEXT *actx, MUTTMENU *menu)
{
  int i;

  if (!menu->tagprefix)
    crypt_pgp_extract_keys_from_attachment_list (CURATTACH->fp, 0, CURATTACH->content);
  else
  {
    for (i = 0; i < actx->idxlen; i++)
      if (actx->idx[i]->content->tagged)
        crypt_pgp_extract_keys_from_attachment_list (actx->idx[i]->fp, 0,
                                                     actx->idx[i]->content);
  }
}

static int recvattach_pgp_check_traditional (ATTACH_CONTEXT *actx, MUTTMENU *menu)
{
  int i, rv = 0;

  if (!menu->tagprefix)
    rv = crypt_pgp_check_traditional (CURATTACH->fp, CURATTACH->content, 1);
  else
  {
    for (i = 0; i < actx->idxlen; i++)
      if (actx->idx[i]->content->tagged)
        rv = rv || crypt_pgp_check_traditional (actx->idx[i]->fp, actx->idx[i]->content, 1);
  }

  return rv;
}

static void recvattach_edit_content_type (ATTACH_CONTEXT *actx, MUTTMENU *menu, HEADER *hdr)
{
  int i;

  if (mutt_edit_content_type (hdr, CURATTACH->content, CURATTACH->fp) == 1)
  {
    /* The mutt_update_recvattach_menu() will overwrite any changes
     * made to a decrypted CURATTACH->content, so warn the user. */
    if (CURATTACH->decrypted)
    {
      mutt_message _("Structural changes to decrypted attachments are not supported");
      mutt_sleep (1);
    }
    /* Editing the content type can rewrite the body structure. */
    for (i = 0; i < actx->idxlen; i++)
      actx->idx[i]->content = NULL;
    mutt_actx_free_entries (actx);
    mutt_update_recvattach_menu (actx, menu, 1);
  }
}

int
mutt_attach_display_loop (MUTTMENU *menu, int op, HEADER *hdr,
			  ATTACH_CONTEXT *actx, int recv)
{
  do
  {
    switch (op)
    {
      case OP_DISPLAY_HEADERS:
	toggle_option (OPTWEED);
	/* fall through */

      case OP_VIEW_ATTACH:
	op = mutt_view_attachment (CURATTACH->fp, CURATTACH->content, MUTT_REGULAR,
				   hdr, actx);
	break;

      case OP_NEXT_ENTRY:
      case OP_MAIN_NEXT_UNDELETED: /* hack */
	if (menu->current < menu->max - 1)
	{
	  menu->current++;
	  op = OP_VIEW_ATTACH;
	}
	else
	  op = OP_NULL;
	break;
      case OP_PREV_ENTRY:
      case OP_MAIN_PREV_UNDELETED: /* hack */
	if (menu->current > 0)
	{
	  menu->current--;
	  op = OP_VIEW_ATTACH;
	}
	else
	  op = OP_NULL;
	break;
      case OP_EDIT_TYPE:
	/* when we edit the content-type, we should redisplay the attachment
	   immediately */
        if (recv)
          recvattach_edit_content_type (actx, menu, hdr);
        else
          mutt_edit_content_type (hdr, CURATTACH->content, CURATTACH->fp);

        menu->redraw |= REDRAW_INDEX;
        op = OP_VIEW_ATTACH;
	break;
      /* functions which are passed through from the pager */
      case OP_CHECK_TRADITIONAL:
        if (!(WithCrypto & APPLICATION_PGP) || (hdr && hdr->security & PGP_TRADITIONAL_CHECKED))
        {
          op = OP_NULL;
          break;
        }
        /* fall through */
      case OP_ATTACH_COLLAPSE:
        if (recv)
          return op;
      default:
	op = OP_NULL;
    }
  }
  while (op != OP_NULL);

#if 0
  if (option (OPTWEED) != old_optweed)
    toggle_option (OPTWEED);
#endif
  return op;
}

static void mutt_generate_recvattach_list (ATTACH_CONTEXT *actx,
                                           HEADER *hdr,
                                           BODY *m,
                                           FILE *fp,
                                           int parent_type,
                                           int level,
                                           int decrypted)
{
  ATTACHPTR *new;
  BODY *new_body = NULL;
  FILE *new_fp = NULL;
  int need_secured, secured;

  for (; m; m = m->next)
  {
    need_secured = secured = 0;

    if ((WithCrypto & APPLICATION_SMIME) &&
        mutt_is_application_smime (m))
    {
      need_secured = 1;

      if (!crypt_valid_passphrase (APPLICATION_SMIME))
        goto decrypt_failed;

      if (hdr->env)
        crypt_smime_getkeys (hdr->env);

      secured = !crypt_smime_decrypt_mime (fp, &new_fp, m, &new_body);

      /* S/MIME nesting */
      if ((mutt_is_application_smime (new_body) & SMIMEOPAQUE) == SMIMEOPAQUE)
      {
        BODY *outer_new_body = new_body;
        FILE *outer_fp = new_fp;

        new_body = NULL;
        new_fp = NULL;

        secured = !crypt_smime_decrypt_mime (outer_fp, &new_fp, outer_new_body,
                                               &new_body);

        mutt_free_body (&outer_new_body);
        safe_fclose (&outer_fp);
      }

      if (secured)
        hdr->security |= SMIMEENCRYPT;
    }

    if ((WithCrypto & APPLICATION_PGP) &&
        (mutt_is_multipart_encrypted (m) ||
         mutt_is_malformed_multipart_pgp_encrypted (m)))
    {
      need_secured = 1;

      if (!crypt_valid_passphrase (APPLICATION_PGP))
        goto decrypt_failed;

      secured = !crypt_pgp_decrypt_mime (fp, &new_fp, m, &new_body);

      if (secured)
        hdr->security |= PGPENCRYPT;
    }

    if (need_secured && secured)
    {
      mutt_actx_add_fp (actx, new_fp);
      mutt_actx_add_body (actx, new_body);
      mutt_generate_recvattach_list (actx, hdr, new_body, new_fp, parent_type, level, 1);
      continue;
    }

decrypt_failed:
    /* Fall through and show the original parts if decryption fails */
    if (need_secured && !secured)
      mutt_error _("Can't decrypt encrypted message!");

    /* Strip out the top level multipart */
    if (m->type == TYPEMULTIPART &&
        m->parts &&
        !need_secured &&
        (parent_type == -1 && ascii_strcasecmp ("alternative", m->subtype)))
    {
      mutt_generate_recvattach_list (actx, hdr, m->parts, fp, m->type, level, decrypted);
    }
    else
    {
      new = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
      mutt_actx_add_attach (actx, new);

      new->content = m;
      new->fp = fp;
      m->aptr = new;
      new->parent_type = parent_type;
      new->level = level;
      new->decrypted = decrypted;

      if (m->type == TYPEMULTIPART)
        mutt_generate_recvattach_list (actx, hdr, m->parts, fp, m->type, level + 1, decrypted);
      else if (mutt_is_message_type (m->type, m->subtype))
      {
        mutt_generate_recvattach_list (actx, m->hdr, m->parts, fp, m->type, level + 1, decrypted);
        hdr->security |= m->hdr->security;
      }
    }
  }
}

void mutt_attach_init (ATTACH_CONTEXT *actx)
{
  int i;

  for (i = 0; i < actx->idxlen; i++)
  {
    actx->idx[i]->content->tagged = 0;
    if (option (OPTDIGESTCOLLAPSE) &&
        actx->idx[i]->content->type == TYPEMULTIPART &&
	!ascii_strcasecmp (actx->idx[i]->content->subtype, "digest"))
      actx->idx[i]->content->collapsed = 1;
    else
      actx->idx[i]->content->collapsed = 0;
  }
}

static void mutt_update_recvattach_menu (ATTACH_CONTEXT *actx, MUTTMENU *menu, int init)
{
  if (init)
  {
    mutt_generate_recvattach_list (actx, actx->hdr, actx->hdr->content,
                                   actx->root_fp, -1, 0, 0);
    mutt_attach_init (actx);
    menu->data = actx;
  }

  mutt_update_tree (actx);

  menu->max  = actx->vcount;

  if (menu->current >= menu->max)
    menu->current = menu->max - 1;
  menu_check_recenter (menu);
  menu->redraw |= REDRAW_INDEX;
}

static void attach_collapse (ATTACH_CONTEXT *actx, MUTTMENU *menu)
{
  int rindex, curlevel;

  CURATTACH->content->collapsed = !CURATTACH->content->collapsed;
  /* When expanding, expand all the children too */
  if (CURATTACH->content->collapsed)
    return;

  curlevel = CURATTACH->level;
  rindex = actx->v2r[menu->current] + 1;

  while ((rindex < actx->idxlen) &&
         (actx->idx[rindex]->level > curlevel))
  {
    if (option (OPTDIGESTCOLLAPSE) &&
        actx->idx[rindex]->content->type == TYPEMULTIPART &&
	!ascii_strcasecmp (actx->idx[rindex]->content->subtype, "digest"))
      actx->idx[rindex]->content->collapsed = 1;
    else
      actx->idx[rindex]->content->collapsed = 0;
    rindex++;
  }
}

static const char *Function_not_permitted = N_("Function not permitted in attach-message mode.");

#define CHECK_ATTACH if(option(OPTATTACHMSG)) \
		     {\
			mutt_flushinp (); \
			mutt_error _(Function_not_permitted); \
			break; \
		     }




void mutt_view_attachments (HEADER *hdr)
{
  char helpstr[LONG_STRING];
  MUTTMENU *menu;
  BODY *cur = NULL;
  MESSAGE *msg;
  ATTACH_CONTEXT *actx;
  int flags = 0;
  int op = OP_NULL;
  int i;

  /* make sure we have parsed this message */
  mutt_parse_mime_message (Context, hdr);

  mutt_message_hook (Context, hdr, MUTT_MESSAGEHOOK);

  if ((msg = mx_open_message (Context, hdr->msgno)) == NULL)
    return;

  menu = mutt_new_menu (MENU_ATTACH);
  menu->title = _("Attachments");
  menu->make_entry = attach_entry;
  menu->tag = mutt_tag_attach;
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_ATTACH, AttachHelp);
  mutt_push_current_menu (menu);

  actx = safe_calloc (sizeof(ATTACH_CONTEXT), 1);
  actx->hdr = hdr;
  actx->root_fp = msg->fp;
  mutt_update_recvattach_menu (actx, menu, 1);

  FOREVER
  {
    if (op == OP_NULL)
      op = mutt_menuLoop (menu);
    switch (op)
    {
      case OP_ATTACH_VIEW_MAILCAP:
	mutt_view_attachment (CURATTACH->fp, CURATTACH->content, MUTT_MAILCAP,
			      hdr, actx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_ATTACH_VIEW_TEXT:
	mutt_view_attachment (CURATTACH->fp, CURATTACH->content, MUTT_AS_TEXT,
			      hdr, actx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_DISPLAY_HEADERS:
      case OP_VIEW_ATTACH:
        op = mutt_attach_display_loop (menu, op, hdr, actx, 1);
        menu->redraw = REDRAW_FULL;
        continue;

      case OP_ATTACH_COLLAPSE:
        if (!CURATTACH->content->parts)
        {
	  mutt_error _("There are no subparts to show!");
	  break;
	}
        attach_collapse (actx, menu);
        mutt_update_recvattach_menu (actx, menu, 0);
        break;

      case OP_FORGET_PASSPHRASE:
        crypt_forget_passphrase ();
        break;

      case OP_EXTRACT_KEYS:
        if ((WithCrypto & APPLICATION_PGP))
        {
          recvattach_extract_pgp_keys (actx, menu);
          menu->redraw = REDRAW_FULL;
        }
        break;

      case OP_CHECK_TRADITIONAL:
        if ((WithCrypto & APPLICATION_PGP) &&
            recvattach_pgp_check_traditional (actx, menu))
        {
	  hdr->security = crypt_query (cur);
	  menu->redraw = REDRAW_FULL;
	}
        break;

      case OP_PRINT:
	mutt_print_attachment_list (actx, CURATTACH->fp, menu->tagprefix,
                                    CURATTACH->content);
	break;

      case OP_PIPE:
	mutt_pipe_attachment_list (actx, CURATTACH->fp, menu->tagprefix,
                                   CURATTACH->content, 0);
	break;

      case OP_SAVE:
	mutt_save_attachment_list (actx, CURATTACH->fp, menu->tagprefix,
                                   CURATTACH->content, hdr, menu);

        if (!menu->tagprefix && option (OPTRESOLVE) && menu->current < menu->max - 1)
	  menu->current++;

        menu->redraw = REDRAW_MOTION_RESYNCH | REDRAW_FULL;
	break;

      case OP_DELETE:
	CHECK_READONLY;

#ifdef USE_POP
	if (Context->magic == MUTT_POP)
	{
	  mutt_flushinp ();
	  mutt_error _("Can't delete attachment from POP server.");
	  break;
	}
#endif

        if (WithCrypto && (hdr->security & ENCRYPT))
        {
          mutt_message _(
            "Deletion of attachments from encrypted messages is unsupported.");
          break;
        }
        if (WithCrypto && (hdr->security & (SIGN | PARTSIGN)))
        {
          mutt_message _(
            "Deletion of attachments from signed messages may invalidate the signature.");
        }
        if (!menu->tagprefix)
        {
          if (CURATTACH->parent_type == TYPEMULTIPART)
          {
            CURATTACH->content->deleted = 1;
            if (option (OPTRESOLVE) && menu->current < menu->max - 1)
            {
              menu->current++;
              menu->redraw = REDRAW_MOTION_RESYNCH;
            }
            else
              menu->redraw = REDRAW_CURRENT;
          }
          else
            mutt_message _(
              "Only deletion of multipart attachments is supported.");
        }
        else
        {
          int x;

          for (x = 0; x < menu->max; x++)
          {
            if (actx->idx[x]->content->tagged)
            {
              if (actx->idx[x]->parent_type == TYPEMULTIPART)
              {
                actx->idx[x]->content->deleted = 1;
                menu->redraw = REDRAW_INDEX;
              }
              else
                mutt_message _(
                  "Only deletion of multipart attachments is supported.");
            }
          }
        }
        break;

      case OP_UNDELETE:
       CHECK_READONLY;
       if (!menu->tagprefix)
       {
	 CURATTACH->content->deleted = 0;
	 if (option (OPTRESOLVE) && menu->current < menu->max - 1)
	 {
	   menu->current++;
	   menu->redraw = REDRAW_MOTION_RESYNCH;
	 }
	 else
	   menu->redraw = REDRAW_CURRENT;
       }
       else
       {
	 int x;

	 for (x = 0; x < menu->max; x++)
	 {
	   if (actx->idx[x]->content->tagged)
	   {
	     actx->idx[x]->content->deleted = 0;
	     menu->redraw = REDRAW_INDEX;
	   }
	 }
       }
       break;

      case OP_RESEND:
        CHECK_ATTACH;
        mutt_attach_resend (CURATTACH->fp, hdr, actx,
                            menu->tagprefix ? NULL : CURATTACH->content);
        menu->redraw = REDRAW_FULL;
      	break;

      case OP_BOUNCE_MESSAGE:
        CHECK_ATTACH;
        mutt_attach_bounce (CURATTACH->fp, hdr, actx,
                            menu->tagprefix ? NULL : CURATTACH->content);
        menu->redraw = REDRAW_FULL;
      	break;

      case OP_FORWARD_MESSAGE:
        CHECK_ATTACH;
        mutt_attach_forward (CURATTACH->fp, hdr, actx,
			     menu->tagprefix ? NULL : CURATTACH->content);
        menu->redraw = REDRAW_FULL;
        break;

      case OP_REPLY:
      case OP_GROUP_REPLY:
      case OP_LIST_REPLY:

        CHECK_ATTACH;

        flags = SENDREPLY |
	  (op == OP_GROUP_REPLY ? SENDGROUPREPLY : 0) |
	  (op == OP_LIST_REPLY ? SENDLISTREPLY : 0);
        mutt_attach_reply (CURATTACH->fp, hdr, actx,
			   menu->tagprefix ? NULL : CURATTACH->content, flags);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_EDIT_TYPE:
        recvattach_edit_content_type (actx, menu, hdr);
        menu->redraw |= REDRAW_INDEX;
	break;

      case OP_EXIT:
	mx_close_message (Context, &msg);

	hdr->attach_del = 0;
        for (i = 0; i < actx->idxlen; i++)
	  if (actx->idx[i]->content &&
              actx->idx[i]->content->deleted)
          {
	    hdr->attach_del = 1;
            break;
          }
	if (hdr->attach_del)
	  hdr->changed = 1;

        mutt_free_attach_context (&actx);

        mutt_pop_current_menu (menu);
	mutt_menuDestroy  (&menu);
	return;
    }

    op = OP_NULL;
  }

  /* not reached */
}
