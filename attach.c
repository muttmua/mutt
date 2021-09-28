/*
 * Copyright (C) 1996-2000,2002,2013 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2004,2006 Thomas Roessler <roessler@does-not-exist.org>
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
#include "attach.h"
#include "mutt_curses.h"
#include "keymap.h"
#include "rfc1524.h"
#include "mime.h"
#include "pager.h"
#include "mailbox.h"
#include "copy.h"
#include "mx.h"
#include "mutt_crypt.h"
#include "rfc3676.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int mutt_get_tmp_attachment (BODY *a)
{
  char type[STRING];
  BUFFER *tempfile = NULL;
  rfc1524_entry *entry = NULL;
  FILE *fpin = NULL, *fpout = NULL;
  struct stat st;

  if (a->unlink)
    return 0;

  tempfile = mutt_buffer_pool_get ();
  entry = rfc1524_new_entry();

  snprintf(type, sizeof(type), "%s/%s", TYPE(a), a->subtype);
  rfc1524_mailcap_lookup(a, type, sizeof(type), entry, 0);
  mutt_rfc1524_expand_filename (entry->nametemplate, a->filename, tempfile);

  rfc1524_free_entry(&entry);

  if (stat(a->filename, &st) == -1)
  {
    mutt_buffer_pool_release (&tempfile);
    return -1;
  }

  if ((fpin = fopen(a->filename, "r")) &&              /* __FOPEN_CHECKED__ */
      (fpout = safe_fopen(mutt_b2s (tempfile), "w")))
  {
    mutt_copy_stream (fpin, fpout);
    mutt_str_replace (&a->filename, mutt_b2s (tempfile));
    a->unlink = 1;

    if (a->stamp >= st.st_mtime)
      mutt_stamp_attachment(a);
  }
  else
    mutt_perror(fpin ? mutt_b2s (tempfile) : a->filename);

  if (fpin)  safe_fclose (&fpin);
  if (fpout) safe_fclose (&fpout);

  mutt_buffer_pool_release (&tempfile);

  return a->unlink ? 0 : -1;
}


/* return 1 if require full screen redraw, 0 otherwise */
int mutt_compose_attachment (BODY *a)
{
  char type[STRING];
  BUFFER *command = mutt_buffer_pool_get ();
  BUFFER *newfile = mutt_buffer_pool_get ();
  BUFFER *tempfile = mutt_buffer_pool_get ();
  rfc1524_entry *entry = rfc1524_new_entry ();
  short unlink_newfile = 0;
  int rc = 0;

  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);
  if (rfc1524_mailcap_lookup (a, type, sizeof(type), entry, MUTT_COMPOSE))
  {
    if (entry->composecommand || entry->composetypecommand)
    {
      if (entry->composetypecommand)
	mutt_buffer_strcpy (command, entry->composetypecommand);
      else
	mutt_buffer_strcpy (command, entry->composecommand);

      mutt_rfc1524_expand_filename (entry->nametemplate,
                                    a->filename, newfile);
      dprint(1, (debugfile, "oldfile: %s\t newfile: %s\n",
                 a->filename, mutt_b2s (newfile)));

      if (safe_symlink (a->filename, mutt_b2s (newfile)) == -1)
      {
        if (mutt_yesorno (_("Can't match nametemplate, continue?"), MUTT_YES) != MUTT_YES)
          goto bailout;
        mutt_buffer_strcpy (newfile, a->filename);
      }
      else
        unlink_newfile = 1;

      if (mutt_rfc1524_expand_command (a, mutt_b2s (newfile), type, command))
      {
	/* For now, editing requires a file, no piping */
	mutt_error _("Mailcap compose entry requires %%s");
      }
      else
      {
	int r;

	mutt_endwin (NULL);
	if ((r = mutt_system (mutt_b2s (command))) == -1)
	  mutt_error (_("Error running \"%s\"!"), mutt_b2s (command));

	if (r != -1 && entry->composetypecommand)
	{
	  BODY *b;
	  FILE *fp, *tfp;

	  if ((fp = safe_fopen (a->filename, "r")) == NULL)
	  {
	    mutt_perror _("Failure to open file to parse headers.");
	    goto bailout;
	  }

	  b = mutt_read_mime_header (fp, 0);
	  if (b)
	  {
	    if (b->parameter)
	    {
	      mutt_free_parameter (&a->parameter);
	      a->parameter = b->parameter;
	      b->parameter = NULL;
	    }
	    if (b->description)
            {
	      FREE (&a->description);
	      a->description = b->description;
	      b->description = NULL;
	    }
	    if (b->form_name)
	    {
	      FREE (&a->form_name);
	      a->form_name = b->form_name;
	      b->form_name = NULL;
	    }

	    /* Remove headers by copying out data to another file, then
	     * copying the file back */
	    fseeko (fp, b->offset, SEEK_SET);
	    mutt_buffer_mktemp (tempfile);
	    if ((tfp = safe_fopen (mutt_b2s (tempfile), "w")) == NULL)
	    {
	      mutt_perror _("Failure to open file to strip headers.");
	      goto bailout;
	    }
	    mutt_copy_stream (fp, tfp);
	    safe_fclose (&fp);
	    safe_fclose (&tfp);
	    mutt_unlink (a->filename);
	    if (mutt_rename_file (mutt_b2s (tempfile), a->filename) != 0)
	    {
	      mutt_perror _("Failure to rename file.");
	      goto bailout;
	    }

	    mutt_free_body (&b);
	  }
	}
      }
    }
  }
  else
  {
    mutt_message (_("No mailcap compose entry for %s, creating empty file."),
                  type);
    rc = 1;
    goto bailout;
  }

  rc = 1;

bailout:

  if (unlink_newfile)
    unlink(mutt_b2s (newfile));

  mutt_buffer_pool_release (&command);
  mutt_buffer_pool_release (&newfile);
  mutt_buffer_pool_release (&tempfile);

  rfc1524_free_entry (&entry);
  return rc;
}

/*
 * Currently, this only works for send mode, as it assumes that the
 * BODY->filename actually contains the information.  I'm not sure
 * we want to deal with editing attachments we've already received,
 * so this should be ok.
 *
 * Returns 1 if editor found, 0 if not (useful to tell calling menu to
 * redraw)
 */
int mutt_edit_attachment (BODY *a)
{
  char type[STRING];
  BUFFER *command = mutt_buffer_pool_get ();
  BUFFER *newfile = mutt_buffer_pool_get ();
  rfc1524_entry *entry = rfc1524_new_entry ();
  short unlink_newfile = 0;
  int rc = 0;

  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);
  if (rfc1524_mailcap_lookup (a, type, sizeof(type), entry, MUTT_EDIT))
  {
    if (entry->editcommand)
    {

      mutt_buffer_strcpy (command, entry->editcommand);
      mutt_rfc1524_expand_filename (entry->nametemplate,
                                    a->filename, newfile);
      dprint(1, (debugfile, "oldfile: %s\t newfile: %s\n",
                 a->filename, mutt_b2s (newfile)));

      if (safe_symlink (a->filename, mutt_b2s (newfile)) == -1)
      {
        if (mutt_yesorno (_("Can't match nametemplate, continue?"), MUTT_YES) != MUTT_YES)
          goto bailout;
        mutt_buffer_strcpy (newfile, a->filename);
      }
      else
        unlink_newfile = 1;

      if (mutt_rfc1524_expand_command (a, mutt_b2s (newfile), type, command))
      {
	/* For now, editing requires a file, no piping */
	mutt_error _("Mailcap Edit entry requires %%s");
        goto bailout;
      }
      else
      {
	mutt_endwin (NULL);
	if (mutt_system (mutt_b2s (command)) == -1)
        {
	  mutt_error (_("Error running \"%s\"!"), mutt_b2s (command));
          goto bailout;
        }
      }
    }
  }
  else if (a->type == TYPETEXT)
  {
    /* On text, default to editor */
    mutt_edit_file (NONULL (Editor), a->filename);
  }
  else
  {
    mutt_error (_("No mailcap edit entry for %s"),type);
    rc = 0;
    goto bailout;
  }

  rc = 1;

bailout:

  if (unlink_newfile)
    unlink(mutt_b2s (newfile));

  mutt_buffer_pool_release (&command);
  mutt_buffer_pool_release (&newfile);

  rfc1524_free_entry (&entry);
  return rc;
}


void mutt_check_lookup_list (BODY *b, char *type, size_t len)
{
  LIST *t = MimeLookupList;
  int i;

  for (; t; t = t->next)
  {
    i = mutt_strlen (t->data) - 1;
    if ((i > 0 && t->data[i-1] == '/' && t->data[i] == '*' &&
	 ascii_strncasecmp (type, t->data, i) == 0) ||
	ascii_strcasecmp (type, t->data) == 0)
    {
      BODY tmp = {0};
      int n;
      if ((n = mutt_lookup_mime_type (&tmp, b->filename)) != TYPEOTHER)
      {
        snprintf (type, len, "%s/%s",
                  n == TYPEAUDIO ? "audio" :
                  n == TYPEAPPLICATION ? "application" :
                  n == TYPEIMAGE ? "image" :
                  n == TYPEMESSAGE ? "message" :
                  n == TYPEMODEL ? "model" :
                  n == TYPEMULTIPART ? "multipart" :
                  n == TYPETEXT ? "text" :
                  n == TYPEVIDEO ? "video" : "other",
                  tmp.subtype);
        dprint(1, (debugfile, "mutt_check_lookup_list: \"%s\" -> %s\n",
                   b->filename, type));
      }
      if (tmp.subtype)
        FREE (&tmp.subtype);
      if (tmp.xtype)
        FREE (&tmp.xtype);
    }
  }
}

/* returns -1 on error, 0 or the return code from mutt_do_pager() on success */
int mutt_view_attachment (FILE *fp, BODY *a, int flag, HEADER *hdr,
			  ATTACH_CONTEXT *actx)
{
  BUFFER *tempfile = NULL;
  BUFFER *pagerfile = NULL;
  BUFFER *command = NULL;
  int is_message;
  int use_mailcap;
  int use_pipe = 0;
  int use_pager = 1;
  char type[STRING];
  char descrip[STRING];
  char *fname;
  rfc1524_entry *entry = NULL;
  int rc = -1;
  int unlink_tempfile = 0, unlink_pagerfile = 0;

  is_message = mutt_is_message_type(a->type, a->subtype);
  if (WithCrypto && is_message && a->hdr && (a->hdr->security & ENCRYPT) &&
      !crypt_valid_passphrase(a->hdr->security))
    return (rc);

  tempfile = mutt_buffer_pool_get ();
  pagerfile = mutt_buffer_pool_get ();
  command = mutt_buffer_pool_get ();

  use_mailcap = (flag == MUTT_MAILCAP ||
                 (flag == MUTT_REGULAR && mutt_needs_mailcap (a)) ||
                 flag == MUTT_VIEW_PAGER);
  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);

  if (use_mailcap)
  {
    entry = rfc1524_new_entry ();
    if (!rfc1524_mailcap_lookup (a, type, sizeof(type), entry,
                                 flag == MUTT_VIEW_PAGER ? MUTT_AUTOVIEW : 0))
    {
      if (flag == MUTT_REGULAR || flag == MUTT_VIEW_PAGER)
      {
	/* fallback to view as text */
	rfc1524_free_entry (&entry);
	mutt_error _("No matching mailcap entry found.  Viewing as text.");
	flag = MUTT_AS_TEXT;
	use_mailcap = 0;
      }
      else
	goto return_error;
    }
  }

  if (use_mailcap)
  {
    if (!entry->command)
    {
      mutt_error _("MIME type not defined.  Cannot view attachment.");
      goto return_error;
    }
    mutt_buffer_strcpy (command, entry->command);

    fname = safe_strdup (a->filename);
    /* In send mode (!fp), we allow slashes because those are part of
     * the tempfile.  The path will be removed in expand_filename */
    mutt_sanitize_filename (fname,
                            (fp ? 0 : MUTT_SANITIZE_ALLOW_SLASH) |
                            MUTT_SANITIZE_ALLOW_8BIT);
    mutt_rfc1524_expand_filename (entry->nametemplate, fname,
                                  tempfile);
    FREE (&fname);

    if (mutt_save_attachment (fp, a, mutt_b2s (tempfile), 0, NULL, 0) == -1)
      goto return_error;
    unlink_tempfile = 1;

    mutt_rfc3676_space_unstuff_attachment (a, mutt_b2s (tempfile));

    use_pipe = mutt_rfc1524_expand_command (a, mutt_b2s (tempfile), type,
                                            command);
    use_pager = entry->copiousoutput;
  }

  if (use_pager)
  {
    if (fp && !use_mailcap && a->filename)
    {
      /* recv case */
      mutt_buffer_strcpy (pagerfile, a->filename);
      mutt_adv_mktemp (pagerfile);
    }
    else
      mutt_buffer_mktemp (pagerfile);
  }

  if (use_mailcap)
  {
    pid_t thepid = 0;
    int tempfd = -1, pagerfd = -1;

    if (!use_pager)
      mutt_endwin (NULL);

    if (use_pager || use_pipe)
    {
      if (use_pager && ((pagerfd = safe_open (mutt_b2s (pagerfile), O_CREAT | O_EXCL | O_WRONLY)) == -1))
      {
	mutt_perror ("open");
	goto return_error;
      }
      unlink_pagerfile = 1;

      if (use_pipe && ((tempfd = open (mutt_b2s (tempfile), 0)) == -1))
      {
	if (pagerfd != -1)
	  close(pagerfd);
	mutt_perror ("open");
	goto return_error;
      }

      if ((thepid = mutt_create_filter_fd (mutt_b2s (command), NULL, NULL, NULL,
					   use_pipe ? tempfd : -1, use_pager ? pagerfd : -1, -1)) == -1)
      {
	if (pagerfd != -1)
	  close(pagerfd);

	if (tempfd != -1)
	  close(tempfd);

	mutt_error _("Cannot create filter");
	goto return_error;
      }

      if (use_pager)
      {
	if (a->description)
	  snprintf (descrip, sizeof (descrip),
		    _("---Command: %-20.20s Description: %s"),
		    mutt_b2s (command), a->description);
	else
	  snprintf (descrip, sizeof (descrip),
		    _("---Command: %-30.30s Attachment: %s"), mutt_b2s (command), type);

        mutt_wait_filter (thepid);
      }
      else
      {
        if (mutt_wait_interactive_filter (thepid) ||
            (entry->needsterminal && option (OPTWAITKEY)))
          mutt_any_key_to_continue (NULL);
      }

      if (tempfd != -1)
	close (tempfd);
      if (pagerfd != -1)
	close (pagerfd);
    }
    else
    {
      /* interactive command */
      if (mutt_system (mutt_b2s (command)) ||
	  (entry->needsterminal && option (OPTWAITKEY)))
	mutt_any_key_to_continue (NULL);
    }
  }
  else
  {
    /* Don't use mailcap; the attachment is viewed in the pager */

    if (flag == MUTT_AS_TEXT)
    {
      /* just let me see the raw data */
      if (fp)
      {
	/* Viewing from a received message.
	 *
	 * Don't use mutt_save_attachment() because we want to perform charset
	 * conversion since this will be displayed by the internal pager.
	 */
	STATE decode_state;

	memset(&decode_state, 0, sizeof(decode_state));
	decode_state.fpout = safe_fopen(mutt_b2s (pagerfile), "w");
	if (!decode_state.fpout)
	{
	  dprint(1, (debugfile, "mutt_view_attachment:%d safe_fopen(%s) errno=%d %s\n",
                     __LINE__, mutt_b2s (pagerfile), errno, strerror(errno)));
	  mutt_perror(mutt_b2s (pagerfile));
	  mutt_sleep(1);
	  goto return_error;
	}
        unlink_pagerfile = 1;

	decode_state.fpin = fp;
	decode_state.flags = MUTT_CHARCONV;
	mutt_decode_attachment(a, &decode_state);
	if (fclose(decode_state.fpout) == EOF)
	  dprint(1, (debugfile, "mutt_view_attachment:%d fclose errno=%d %s\n",
                     __LINE__, mutt_b2s (pagerfile), errno, strerror(errno)));
      }
      else
      {
	/* in compose mode, just copy the file.  we can't use
	 * mutt_decode_attachment() since it assumes the content-encoding has
	 * already been applied
	 */
	if (mutt_save_attachment (fp, a, mutt_b2s (pagerfile), 0, NULL, 0))
	  goto return_error;
        unlink_pagerfile = 1;
      }
      mutt_rfc3676_space_unstuff_attachment (a, mutt_b2s (pagerfile));
    }
    else
    {
      /* Use built-in handler */
      set_option (OPTVIEWATTACH); /* disable the "use 'v' to view this part"
				   * message in case of error */
      if (mutt_decode_save_attachment (fp, a, mutt_b2s (pagerfile), MUTT_DISPLAY, 0))
      {
	unset_option (OPTVIEWATTACH);
	goto return_error;
      }
      unlink_pagerfile = 1;
      unset_option (OPTVIEWATTACH);
    }

    if (a->description)
      strfcpy (descrip, a->description, sizeof (descrip));
    else if (a->filename)
      snprintf (descrip, sizeof (descrip), _("---Attachment: %s: %s"),
                a->filename, type);
    else
      snprintf (descrip, sizeof (descrip), _("---Attachment: %s"), type);
  }

  /* We only reach this point if there have been no errors */

  if (use_pager)
  {
    pager_t info;

    memset (&info, 0, sizeof (info));
    info.fp = fp;
    info.bdy = a;
    info.ctx = Context;
    info.actx = actx;
    info.hdr = hdr;

    rc = mutt_do_pager (descrip, mutt_b2s (pagerfile),
			MUTT_PAGER_ATTACHMENT | (is_message ? MUTT_PAGER_MESSAGE : 0), &info);
    unlink_pagerfile = 0;
  }
  else
    rc = 0;

return_error:

  rfc1524_free_entry (&entry);
  if (unlink_tempfile)
    mutt_unlink (mutt_b2s (tempfile));
  if (unlink_pagerfile)
    mutt_unlink (mutt_b2s (pagerfile));

  mutt_buffer_pool_release (&tempfile);
  mutt_buffer_pool_release (&pagerfile);
  mutt_buffer_pool_release (&command);

  return rc;
}

/* returns 1 on success, 0 on error */
int mutt_pipe_attachment (FILE *fp, BODY *b, const char *path, const char *outfile)
{
  pid_t thepid = 0;
  int out = -1, rv = 0, is_flowed = 0, unlink_unstuff = 0;
  FILE *filter_fp = NULL, *unstuff_fp = NULL, *ifp = NULL;
  BUFFER *unstuff_tempfile = NULL;

  if (outfile && *outfile)
    if ((out = safe_open (outfile, O_CREAT | O_EXCL | O_WRONLY)) < 0)
    {
      mutt_perror ("open");
      return 0;
    }

  if (mutt_rfc3676_is_format_flowed (b))
  {
    is_flowed = 1;
    unstuff_tempfile = mutt_buffer_pool_get ();
    mutt_buffer_mktemp (unstuff_tempfile);
  }

  mutt_endwin (NULL);

  if (outfile && *outfile)
    thepid = mutt_create_filter_fd (path, &filter_fp, NULL, NULL, -1, out, -1);
  else
    thepid = mutt_create_filter (path, &filter_fp, NULL, NULL);
  if (thepid < 0)
  {
    mutt_perror _("Can't create filter");
    goto bail;
  }

  /* recv case */
  if (fp)
  {
    STATE s;

    memset (&s, 0, sizeof (STATE));
    /* perform charset conversion on text attachments when piping */
    s.flags = MUTT_CHARCONV;

    if (is_flowed)
    {
      unstuff_fp = safe_fopen (mutt_b2s (unstuff_tempfile), "w");
      if (unstuff_fp == NULL)
      {
        mutt_perror ("safe_fopen");
        goto bail;
      }
      unlink_unstuff = 1;

      s.fpin = fp;
      s.fpout = unstuff_fp;
      mutt_decode_attachment (b, &s);
      safe_fclose (&unstuff_fp);

      mutt_rfc3676_space_unstuff_attachment (b, mutt_b2s (unstuff_tempfile));

      unstuff_fp = safe_fopen (mutt_b2s (unstuff_tempfile), "r");
      if (unstuff_fp == NULL)
      {
        mutt_perror ("safe_fopen");
        goto bail;
      }
      mutt_copy_stream (unstuff_fp, filter_fp);
      safe_fclose (&unstuff_fp);
    }
    else
    {
      s.fpin = fp;
      s.fpout = filter_fp;
      mutt_decode_attachment (b, &s);
    }
  }

  /* send case */
  else
  {
    const char *infile;

    if (is_flowed)
    {
      if (mutt_save_attachment (fp, b, mutt_b2s (unstuff_tempfile), 0, NULL, 0) == -1)
        goto bail;
      unlink_unstuff = 1;
      mutt_rfc3676_space_unstuff_attachment (b, mutt_b2s (unstuff_tempfile));
      infile = mutt_b2s (unstuff_tempfile);
    }
    else
      infile = b->filename;

    if ((ifp = fopen (infile, "r")) == NULL)
    {
      mutt_perror ("fopen");
      goto bail;
    }
    mutt_copy_stream (ifp, filter_fp);
    safe_fclose (&ifp);
  }

  safe_fclose (&filter_fp);
  rv = 1;

bail:
  if (outfile && *outfile)
  {
    close (out);
    if (rv == 0)
      unlink (outfile);
    else if (is_flowed)
      mutt_rfc3676_space_stuff_attachment (NULL, outfile);
  }

  safe_fclose (&unstuff_fp);
  safe_fclose (&filter_fp);
  safe_fclose (&ifp);

  if (unlink_unstuff)
    mutt_unlink (mutt_b2s (unstuff_tempfile));
  mutt_buffer_pool_release (&unstuff_tempfile);

  /*
   * check for error exit from child process
   */
  if ((thepid > 0) && (mutt_wait_filter (thepid) != 0))
    rv = 0;

  if (rv == 0 || option (OPTWAITKEY))
    mutt_any_key_to_continue (NULL);
  return rv;
}

static FILE *
mutt_save_attachment_open (const char *path, int flags)
{
  if (flags == MUTT_SAVE_APPEND)
    return fopen (path, "a");
  if (flags == MUTT_SAVE_OVERWRITE)
    return fopen (path, "w");		/* __FOPEN_CHECKED__ */

  return safe_fopen (path, "w");
}

/* returns 0 on success, -1 on error */
int mutt_save_attachment (FILE *fp, BODY *m, const char *path, int flags, HEADER *hdr,
                          int charset_conv)
{
  if (fp)
  {

    /* recv mode */

    if (hdr &&
        m->hdr &&
        m->encoding != ENCBASE64 &&
        m->encoding != ENCQUOTEDPRINTABLE &&
        mutt_is_message_type(m->type, m->subtype))
    {
      /* message type attachments are written to mail folders. */

      char buf[HUGE_STRING];
      HEADER *hn;
      CONTEXT ctx;
      MESSAGE *msg;
      int chflags = 0;
      int r = -1;

      hn = m->hdr;
      hn->msgno = hdr->msgno; /* required for MH/maildir */
      hn->read = 1;

      fseeko (fp, m->offset, SEEK_SET);
      if (fgets (buf, sizeof (buf), fp) == NULL)
	return -1;
      if (mx_open_mailbox(path, MUTT_APPEND | MUTT_QUIET, &ctx) == NULL)
	return -1;
      if ((msg = mx_open_new_message (&ctx, hn, is_from (buf, NULL, 0, NULL) ? 0 : MUTT_ADD_FROM)) == NULL)
      {
	mx_close_mailbox(&ctx, NULL);
	return -1;
      }
      if (ctx.magic == MUTT_MBOX || ctx.magic == MUTT_MMDF)
	chflags = CH_FROM | CH_UPDATE_LEN;
      chflags |= (ctx.magic == MUTT_MAILDIR ? CH_NOSTATUS : CH_UPDATE);
      if (_mutt_copy_message (msg->fp, fp, hn, hn->content, 0, chflags) == 0
	  && mx_commit_message (msg, &ctx) == 0)
	r = 0;
      else
	r = -1;

      mx_close_message (&ctx, &msg);
      mx_close_mailbox (&ctx, NULL);
      return r;
    }
    else
    {
      /* In recv mode, extract from folder and decode */

      STATE s;

      memset (&s, 0, sizeof (s));
      if (charset_conv)
        s.flags = MUTT_CHARCONV;

      if ((s.fpout = mutt_save_attachment_open (path, flags)) == NULL)
      {
	mutt_perror ("fopen");
	mutt_sleep (2);
	return (-1);
      }
      fseeko ((s.fpin = fp), m->offset, SEEK_SET);
      mutt_decode_attachment (m, &s);

      if (fclose (s.fpout) != 0)
      {
	mutt_perror ("fclose");
	mutt_sleep (2);
	return (-1);
      }
    }
  }
  else
  {
    /* In send mode, just copy file */

    FILE *ofp, *nfp;

    if ((ofp = fopen (m->filename, "r")) == NULL)
    {
      mutt_perror ("fopen");
      return (-1);
    }

    if ((nfp = mutt_save_attachment_open (path, flags)) == NULL)
    {
      mutt_perror ("fopen");
      safe_fclose (&ofp);
      return (-1);
    }

    if (mutt_copy_stream (ofp, nfp) == -1)
    {
      mutt_error _("Write fault!");
      safe_fclose (&ofp);
      safe_fclose (&nfp);
      return (-1);
    }
    safe_fclose (&ofp);
    safe_fclose (&nfp);
  }

  return 0;
}

/* returns 0 on success, -1 on error */
int mutt_decode_save_attachment (FILE *fp, BODY *m, const char *path,
				 int displaying, int flags)
{
  STATE s;
  unsigned int saved_encoding = 0;
  BODY *saved_parts = NULL;
  HEADER *saved_hdr = NULL;

  memset (&s, 0, sizeof (s));
  s.flags = displaying;

  if (flags == MUTT_SAVE_APPEND)
    s.fpout = fopen (path, "a");
  else if (flags == MUTT_SAVE_OVERWRITE)
    s.fpout = fopen (path, "w");	/* __FOPEN_CHECKED__ */
  else
    s.fpout = safe_fopen (path, "w");

  if (s.fpout == NULL)
  {
    mutt_perror ("fopen");
    return (-1);
  }

  if (fp == NULL)
  {
    /* When called from the compose menu, the attachment isn't parsed,
     * so we need to do it here. */
    struct stat st;

    if (stat (m->filename, &st) == -1)
    {
      mutt_perror ("stat");
      safe_fclose (&s.fpout);
      return (-1);
    }

    if ((s.fpin = fopen (m->filename, "r")) == NULL)
    {
      mutt_perror ("fopen");
      return (-1);
    }

    saved_encoding = m->encoding;
    if (!is_multipart (m))
      m->encoding = ENC8BIT;

    m->length = st.st_size;
    m->offset = 0;
    saved_parts = m->parts;
    saved_hdr = m->hdr;
    mutt_parse_part (s.fpin, m);

    if (m->noconv || is_multipart (m))
      s.flags |= MUTT_CHARCONV;
  }
  else
  {
    s.fpin = fp;
    s.flags |= MUTT_CHARCONV;
  }

  mutt_body_handler (m, &s);

  safe_fclose (&s.fpout);
  if (fp == NULL)
  {
    m->length = 0;
    m->encoding = saved_encoding;
    if (saved_parts)
    {
      mutt_free_header (&m->hdr);
      m->parts = saved_parts;
      m->hdr = saved_hdr;
    }
    safe_fclose (&s.fpin);
  }

  return (0);
}

/* Ok, the difference between send and receive:
 * recv: BODY->filename is a suggested name, and Context|HEADER points
 *       to the attachment in mailbox which is encoded
 * send: BODY->filename points to the un-encoded file which contains the
 *       attachment
 */

int mutt_print_attachment (FILE *fp, BODY *a)
{
  BUFFER *newfile = mutt_buffer_pool_get ();
  BUFFER *command = mutt_buffer_pool_get ();
  char type[STRING];
  pid_t thepid;
  FILE *ifp, *fpout;
  short unlink_newfile = 0;
  int rc = 0;

  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);

  if (rfc1524_mailcap_lookup (a, type, sizeof(type), NULL, MUTT_PRINT))
  {
    rfc1524_entry *entry = NULL;
    int piped = 0;
    char *sanitized_fname = NULL;

    dprint (2, (debugfile, "Using mailcap...\n"));

    entry = rfc1524_new_entry ();
    rfc1524_mailcap_lookup (a, type, sizeof(type), entry, MUTT_PRINT);

    sanitized_fname = safe_strdup (a->filename);
    /* In send mode (!fp), we allow slashes because those are part of
     * the tempfile.  The path will be removed in expand_filename */
    mutt_sanitize_filename (sanitized_fname,
                            (fp ? 0 : MUTT_SANITIZE_ALLOW_SLASH) |
                            MUTT_SANITIZE_ALLOW_8BIT);
    mutt_rfc1524_expand_filename (entry->nametemplate, sanitized_fname,
                                  newfile);
    FREE (&sanitized_fname);

    if (mutt_save_attachment (fp, a, mutt_b2s (newfile), 0, NULL, 0) == -1)
      goto mailcap_cleanup;
    unlink_newfile = 1;

    mutt_rfc3676_space_unstuff_attachment (a, mutt_b2s (newfile));

    mutt_buffer_strcpy (command, entry->printcommand);
    piped = mutt_rfc1524_expand_command (a, mutt_b2s (newfile), type, command);

    mutt_endwin (NULL);

    /* interactive program */
    if (piped)
    {
      if ((ifp = fopen (mutt_b2s (newfile), "r")) == NULL)
      {
	mutt_perror ("fopen");
	goto mailcap_cleanup;
      }

      if ((thepid = mutt_create_filter (mutt_b2s (command), &fpout, NULL, NULL)) < 0)
      {
	mutt_perror _("Can't create filter");
	safe_fclose (&ifp);
	goto mailcap_cleanup;
      }
      mutt_copy_stream (ifp, fpout);
      safe_fclose (&fpout);
      safe_fclose (&ifp);
      if (mutt_wait_filter (thepid) || option (OPTWAITKEY))
	mutt_any_key_to_continue (NULL);
    }
    else
    {
      if (mutt_system (mutt_b2s (command)) || option (OPTWAITKEY))
	mutt_any_key_to_continue (NULL);
    }

    rc = 1;

  mailcap_cleanup:
    if (unlink_newfile)
      mutt_unlink (mutt_b2s (newfile));

    rfc1524_free_entry (&entry);
    goto out;
  }

  if (!ascii_strcasecmp ("text/plain", type) ||
      !ascii_strcasecmp ("application/postscript", type))
  {
    rc = mutt_pipe_attachment (fp, a, NONULL(PrintCmd), NULL);
    goto out;
  }
  else if (mutt_can_decode (a))
  {
    /* decode and print */

    ifp = NULL;
    fpout = NULL;

    mutt_buffer_mktemp (newfile);
    if (mutt_decode_save_attachment (fp, a, mutt_b2s (newfile), MUTT_PRINTING, 0) == 0)
    {
      unlink_newfile = 1;
      dprint (2, (debugfile, "successfully decoded %s type attachment to %s\n",
		  type, mutt_b2s (newfile)));

      if ((ifp = fopen (mutt_b2s (newfile), "r")) == NULL)
      {
	mutt_perror ("fopen");
	goto decode_cleanup;
      }

      dprint (2, (debugfile, "successfully opened %s read-only\n", mutt_b2s (newfile)));

      mutt_endwin (NULL);
      if ((thepid = mutt_create_filter (NONULL(PrintCmd), &fpout, NULL, NULL)) < 0)
      {
	mutt_perror _("Can't create filter");
	goto decode_cleanup;
      }

      dprint (2, (debugfile, "Filter created.\n"));

      mutt_copy_stream (ifp, fpout);

      safe_fclose (&fpout);
      safe_fclose (&ifp);

      if (mutt_wait_filter (thepid) != 0 || option (OPTWAITKEY))
	mutt_any_key_to_continue (NULL);
      rc = 1;
    }
  decode_cleanup:
    safe_fclose (&ifp);
    safe_fclose (&fpout);
    if (unlink_newfile)
      mutt_unlink (mutt_b2s (newfile));
  }
  else
  {
    mutt_error _("I don't know how to print that!");
    rc = 0;
  }

out:
  mutt_buffer_pool_release (&newfile);
  mutt_buffer_pool_release (&command);

  return rc;
}

void mutt_actx_add_attach (ATTACH_CONTEXT *actx, ATTACHPTR *attach)
{
  int i;

  if (actx->idxlen == actx->idxmax)
  {
    actx->idxmax += 5;
    safe_realloc (&actx->idx, sizeof (ATTACHPTR *) * actx->idxmax);
    safe_realloc (&actx->v2r, sizeof (short) * actx->idxmax);
    for (i = actx->idxlen; i < actx->idxmax; i++)
      actx->idx[i] = NULL;
  }

  actx->idx[actx->idxlen++] = attach;
}

void mutt_actx_add_fp (ATTACH_CONTEXT *actx, FILE *new_fp)
{
  int i;

  if (actx->fp_len == actx->fp_max)
  {
    actx->fp_max += 5;
    safe_realloc (&actx->fp_idx, sizeof (FILE *) * actx->fp_max);
    for (i = actx->fp_len; i < actx->fp_max; i++)
      actx->fp_idx[i] = NULL;
  }

  actx->fp_idx[actx->fp_len++] = new_fp;
}

void mutt_actx_add_body (ATTACH_CONTEXT *actx, BODY *new_body)
{
  int i;

  if (actx->body_len == actx->body_max)
  {
    actx->body_max += 5;
    safe_realloc (&actx->body_idx, sizeof (BODY *) * actx->body_max);
    for (i = actx->body_len; i < actx->body_max; i++)
      actx->body_idx[i] = NULL;
  }

  actx->body_idx[actx->body_len++] = new_body;
}

void mutt_actx_free_entries (ATTACH_CONTEXT *actx)
{
  int i;

  for (i = 0; i < actx->idxlen; i++)
  {
    if (actx->idx[i]->content)
      actx->idx[i]->content->aptr = NULL;
    FREE (&actx->idx[i]->tree);
    FREE (&actx->idx[i]);
  }
  actx->idxlen = 0;
  actx->vcount = 0;

  for (i = 0; i < actx->fp_len; i++)
    safe_fclose (&actx->fp_idx[i]);
  actx->fp_len = 0;

  for (i = 0; i < actx->body_len; i++)
    mutt_free_body (&actx->body_idx[i]);
  actx->body_len = 0;
}

void mutt_free_attach_context (ATTACH_CONTEXT **pactx)
{
  ATTACH_CONTEXT *actx;

  if (!pactx || !*pactx)
    return;

  actx = *pactx;
  mutt_actx_free_entries (actx);
  FREE (&actx->idx);
  FREE (&actx->v2r);
  FREE (&actx->fp_idx);
  FREE (&actx->body_idx);
  FREE (pactx);  /* __FREE_CHECKED__ */
}
