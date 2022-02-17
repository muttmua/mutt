/*
 * Copyright (C) 1996-1998,2010,2012 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1996-1999 Brandon Long <blong@fiction.net>
 * Copyright (C) 1999-2009,2011 Brendan Cully <brendan@kublai.com>
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

/* command.c: routines for sending commands to an IMAP server and parsing
 *  responses */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "mutt_menu.h"
#include "imap_private.h"
#include "mx.h"
#include "buffy.h"

#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#define IMAP_CMD_BUFSIZE 512

/* forward declarations */
static int cmd_start (IMAP_DATA* idata, const char* cmdstr, int flags);
static int cmd_queue_full (IMAP_DATA* idata);
static int cmd_queue (IMAP_DATA* idata, const char* cmdstr, int flags);
static IMAP_COMMAND* cmd_new (IMAP_DATA* idata);
static int cmd_status (const char *s);
static void cmd_handle_fatal (IMAP_DATA* idata);
static int cmd_handle_untagged (IMAP_DATA* idata);
static void cmd_parse_capability (IMAP_DATA* idata, char* s);
static void cmd_parse_vanished (IMAP_DATA* idata, char* s);
static void cmd_parse_expunge (IMAP_DATA* idata, const char* s);
static void cmd_parse_list (IMAP_DATA* idata, char* s);
static void cmd_parse_lsub (IMAP_DATA* idata, char* s);
static void cmd_parse_fetch (IMAP_DATA* idata, char* s);
static void cmd_parse_myrights (IMAP_DATA* idata, const char* s);
static void cmd_parse_search (IMAP_DATA* idata, const char* s);
static void cmd_parse_status (IMAP_DATA* idata, char* s);
static void cmd_parse_enabled (IMAP_DATA* idata, const char* s);

static const char * const Capabilities[] = {
  "IMAP4",
  "IMAP4rev1",
  "STATUS",
  "ACL",
  "NAMESPACE",
  "AUTH=CRAM-MD5",
  "AUTH=GSSAPI",
  "AUTH=ANONYMOUS",
  "AUTH=OAUTHBEARER",
  "AUTH=XOAUTH2",
  "STARTTLS",
  "LOGINDISABLED",
  "IDLE",
  "SASL-IR",
  "ENABLE",
  "CONDSTORE",
  "QRESYNC",
  "LIST-EXTENDED",
  "COMPRESS=DEFLATE",

  NULL
};

/* imap_cmd_start: Given an IMAP command, send it to the server.
 *   If cmdstr is NULL, sends queued commands. */
int imap_cmd_start (IMAP_DATA* idata, const char* cmdstr)
{
  return cmd_start (idata, cmdstr, 0);
}

/* imap_cmd_step: Reads server responses from an IMAP command, detects
 *   tagged completion response, handles untagged messages, can read
 *   arbitrarily large strings (using malloc, so don't make it _too_
 *   large!). */
int imap_cmd_step (IMAP_DATA* idata)
{
  size_t len = 0;
  int c;
  int rc;
  int stillrunning = 0;
  IMAP_COMMAND* cmd;

  if (idata->status == IMAP_FATAL)
  {
    cmd_handle_fatal (idata);
    return IMAP_CMD_BAD;
  }

  /* read into buffer, expanding buffer as necessary until we have a full
   * line */
  do
  {
    if (len == idata->blen)
    {
      safe_realloc (&idata->buf, idata->blen + IMAP_CMD_BUFSIZE);
      idata->blen = idata->blen + IMAP_CMD_BUFSIZE;
      dprint (3, (debugfile, "imap_cmd_step: grew buffer to %u bytes\n",
		  idata->blen));
    }

    /* back up over '\0' */
    if (len)
      len--;
    c = mutt_socket_readln (idata->buf + len, idata->blen - len, idata->conn);
    if (c <= 0)
    {
      dprint (1, (debugfile, "imap_cmd_step: Error reading server response.\n"));
      cmd_handle_fatal (idata);
      return IMAP_CMD_BAD;
    }

    len += c;
  }
  /* if we've read all the way to the end of the buffer, we haven't read a
   * full line (mutt_socket_readln strips the \r, so we always have at least
   * one character free when we've read a full line) */
  while (len == idata->blen);

  /* don't let one large string make cmd->buf hog memory forever */
  if ((idata->blen > IMAP_CMD_BUFSIZE) && (len <= IMAP_CMD_BUFSIZE))
  {
    safe_realloc (&idata->buf, IMAP_CMD_BUFSIZE);
    idata->blen = IMAP_CMD_BUFSIZE;
    dprint (3, (debugfile, "imap_cmd_step: shrank buffer to %u bytes\n", idata->blen));
  }

  idata->lastread = time (NULL);

  /* handle untagged messages. The caller still gets its shot afterwards. */
  if ((!ascii_strncmp (idata->buf, "* ", 2)
       || !ascii_strncmp (imap_next_word (idata->buf), "OK [", 4))
      && cmd_handle_untagged (idata))
    return IMAP_CMD_BAD;

  /* server demands a continuation response from us */
  if (idata->buf[0] == '+')
    return IMAP_CMD_RESPOND;

  /* Look for tagged command completions.
   *
   * Some response handlers can end up recursively calling
   * imap_cmd_step() and end up handling all tagged command
   * completions.
   * (e.g. FETCH->set_flag->set_header_color->~h pattern match.)
   *
   * Other callers don't even create an idata->cmds entry.
   *
   * For both these cases, we default to returning OK */
  rc = IMAP_CMD_OK;
  c = idata->lastcmd;
  do
  {
    cmd = &idata->cmds[c];
    if (cmd->state == IMAP_CMD_NEW)
    {
      if (!ascii_strncmp (idata->buf, cmd->seq, SEQLEN))
      {
	if (!stillrunning)
	{
	  /* first command in queue has finished - move queue pointer up */
	  idata->lastcmd = (idata->lastcmd + 1) % idata->cmdslots;
	}
	cmd->state = cmd_status (idata->buf);
	/* bogus - we don't know which command result to return here. Caller
	 * should provide a tag. */
	rc = cmd->state;
      }
      else
	stillrunning++;
    }

    c = (c + 1) % idata->cmdslots;
  }
  while (c != idata->nextcmd);

  if (stillrunning)
    rc = IMAP_CMD_CONTINUE;
  else
  {
    dprint (3, (debugfile, "IMAP queue drained\n"));
    imap_cmd_finish (idata);
  }


  return rc;
}

/* imap_code: returns 1 if the command result was OK, or 0 if NO or BAD */
int imap_code (const char *s)
{
  return cmd_status (s) == IMAP_CMD_OK;
}

/* imap_cmd_trailer: extra information after tagged command response if any */
const char* imap_cmd_trailer (IMAP_DATA* idata)
{
  static const char* notrailer = "";
  const char* s = idata->buf;

  if (!s)
  {
    dprint (2, (debugfile, "imap_cmd_trailer: not a tagged response"));
    return notrailer;
  }

  s = imap_next_word ((char *)s);
  if (!s || (ascii_strncasecmp (s, "OK", 2) &&
	     ascii_strncasecmp (s, "NO", 2) &&
	     ascii_strncasecmp (s, "BAD", 3)))
  {
    dprint (2, (debugfile, "imap_cmd_trailer: not a command completion: %s",
		idata->buf));
    return notrailer;
  }

  s = imap_next_word ((char *)s);
  if (!s)
    return notrailer;

  return s;
}

/* imap_exec: execute a command, and wait for the response from the server.
 * Also, handle untagged responses.
 * Flags:
 *   IMAP_CMD_FAIL_OK: the calling procedure can handle failure. This is used
 *     for checking for a mailbox on append and login
 *   IMAP_CMD_PASS: command contains a password. Suppress logging.
 *   IMAP_CMD_QUEUE: only queue command, do not execute.
 *   IMAP_CMD_POLL: poll the socket for a response before running imap_cmd_step.
 * Return 0 on success, -1 on Failure, -2 on OK Failure
 */
int imap_exec (IMAP_DATA* idata, const char* cmdstr, int flags)
{
  int rc;

  if ((rc = cmd_start (idata, cmdstr, flags)) < 0)
  {
    cmd_handle_fatal (idata);
    return -1;
  }

  if (flags & IMAP_CMD_QUEUE)
    return 0;

  if ((flags & IMAP_CMD_POLL) &&
      (ImapPollTimeout > 0) &&
      (mutt_socket_poll (idata->conn, ImapPollTimeout)) == 0)
  {
    mutt_error (_("Connection to %s timed out"), idata->conn->account.host);
    mutt_sleep (0);
    cmd_handle_fatal (idata);
    return -1;
  }

  do
    rc = imap_cmd_step (idata);
  while (rc == IMAP_CMD_CONTINUE);

  if (rc == IMAP_CMD_NO && (flags & IMAP_CMD_FAIL_OK))
    return -2;

  if (rc != IMAP_CMD_OK)
  {
    if ((flags & IMAP_CMD_FAIL_OK) && idata->status != IMAP_FATAL)
      return -2;

    dprint (1, (debugfile, "imap_exec: command failed: %s\n", idata->buf));
    return -1;
  }

  return 0;
}

/* imap_cmd_finish
 *
 * If a reopen is allowed, it attempts to perform cleanup (eg fetch new
 * mail if detected, do expunge). Called automatically by
 * imap_cmd_step(), but may be called at any time.
 *
 * idata->check_status is set and will be used later by
 * imap_check_mailbox().
 */
void imap_cmd_finish (IMAP_DATA* idata)
{
  if (idata->status == IMAP_FATAL)
  {
    cmd_handle_fatal (idata);
    return;
  }

  if (!(idata->state >= IMAP_SELECTED) || idata->ctx->closing)
    return;

  if (idata->reopen & IMAP_REOPEN_ALLOW)
  {
    if (idata->reopen & IMAP_EXPUNGE_PENDING)
    {
      dprint (2, (debugfile, "imap_cmd_finish: Expunging mailbox\n"));
      imap_expunge_mailbox (idata);
      /* Detect whether we've gotten unexpected EXPUNGE messages */
      if (!(idata->reopen & IMAP_EXPUNGE_EXPECTED))
	idata->check_status |= IMAP_EXPUNGE_PENDING;
      idata->reopen &= ~(IMAP_EXPUNGE_PENDING | IMAP_EXPUNGE_EXPECTED);
    }
    if (idata->reopen & IMAP_NEWMAIL_PENDING)
    {
      dprint (2, (debugfile, "imap_cmd_finish: Fetching new mail\n"));
      imap_read_headers (idata, idata->max_msn+1, idata->newMailCount, 0);
      idata->check_status |= IMAP_NEWMAIL_PENDING;
    }
  }

  idata->status = 0;
}

/* imap_cmd_idle: Enter the IDLE state. */
int imap_cmd_idle (IMAP_DATA* idata)
{
  int rc;

  if (cmd_start (idata, "IDLE", IMAP_CMD_POLL) < 0)
  {
    cmd_handle_fatal (idata);
    return -1;
  }

  if ((ImapPollTimeout > 0) &&
      (mutt_socket_poll (idata->conn, ImapPollTimeout)) == 0)
  {
    mutt_error (_("Connection to %s timed out"), idata->conn->account.host);
    mutt_sleep (0);
    cmd_handle_fatal (idata);
    return -1;
  }

  do
    rc = imap_cmd_step (idata);
  while (rc == IMAP_CMD_CONTINUE);

  if (rc == IMAP_CMD_RESPOND)
  {
    /* successfully entered IDLE state */
    idata->state = IMAP_IDLE;
    /* queue automatic exit when next command is issued */
    mutt_buffer_addstr (idata->cmdbuf, "DONE\r\n");
    rc = IMAP_CMD_OK;
  }
  if (rc != IMAP_CMD_OK)
  {
    dprint (1, (debugfile, "imap_cmd_idle: error starting IDLE\n"));
    return -1;
  }

  return 0;
}

static int cmd_queue_full (IMAP_DATA* idata)
{
  if ((idata->nextcmd + 1) % idata->cmdslots == idata->lastcmd)
    return 1;

  return 0;
}

/* sets up a new command control block and adds it to the queue.
 * Returns NULL if the pipeline is full. */
static IMAP_COMMAND* cmd_new (IMAP_DATA* idata)
{
  IMAP_COMMAND* cmd;

  if (cmd_queue_full (idata))
  {
    dprint (3, (debugfile, "cmd_new: IMAP command queue full\n"));
    return NULL;
  }

  cmd = idata->cmds + idata->nextcmd;
  idata->nextcmd = (idata->nextcmd + 1) % idata->cmdslots;

  snprintf (cmd->seq, sizeof (cmd->seq), "a%04u", idata->seqno++);
  if (idata->seqno > 9999)
    idata->seqno = 0;

  cmd->state = IMAP_CMD_NEW;

  return cmd;
}

/* queues command. If the queue is full, attempts to drain it. */
static int cmd_queue (IMAP_DATA* idata, const char* cmdstr, int flags)
{
  IMAP_COMMAND* cmd;
  int rc;

  if (cmd_queue_full (idata))
  {
    dprint (3, (debugfile, "Draining IMAP command pipeline\n"));

    rc = imap_exec (idata, NULL, IMAP_CMD_FAIL_OK | (flags & IMAP_CMD_POLL));

    if (rc < 0 && rc != -2)
      return rc;
  }

  if (!(cmd = cmd_new (idata)))
    return IMAP_CMD_BAD;

  if (mutt_buffer_add_printf (idata->cmdbuf, "%s %s\r\n", cmd->seq, cmdstr) < 0)
    return IMAP_CMD_BAD;

  return 0;
}

static int cmd_start (IMAP_DATA* idata, const char* cmdstr, int flags)
{
  int rc;

  if (idata->status == IMAP_FATAL)
  {
    cmd_handle_fatal (idata);
    return -1;
  }

  if (cmdstr && ((rc = cmd_queue (idata, cmdstr, flags)) < 0))
    return rc;

  if (flags & IMAP_CMD_QUEUE)
    return 0;

  if (mutt_buffer_len (idata->cmdbuf) == 0)
    return IMAP_CMD_BAD;

  rc = mutt_socket_write_d (idata->conn, idata->cmdbuf->data, -1,
                            flags & IMAP_CMD_PASS ? IMAP_LOG_PASS : IMAP_LOG_CMD);
  mutt_buffer_clear (idata->cmdbuf);

  /* unidle when command queue is flushed */
  if (idata->state == IMAP_IDLE)
    idata->state = IMAP_SELECTED;

  return (rc < 0) ? IMAP_CMD_BAD : 0;
}

/* parse response line for tagged OK/NO/BAD */
static int cmd_status (const char *s)
{
  s = imap_next_word((char*)s);

  if (!ascii_strncasecmp("OK", s, 2))
    return IMAP_CMD_OK;
  if (!ascii_strncasecmp("NO", s, 2))
    return IMAP_CMD_NO;

  return IMAP_CMD_BAD;
}

/* cmd_handle_fatal: when IMAP_DATA is in fatal state, do what we can */
static void cmd_handle_fatal (IMAP_DATA* idata)
{
  /* Attempt to reconnect later during mx_check_mailbox() */
  if (Context && idata->ctx == Context)
  {
    if (idata->status != IMAP_FATAL)
    {
      idata->status = IMAP_FATAL;
      /* L10N:
         When a fatal error occurs with the IMAP connection for
         the currently open mailbox, we print this message, and
         will try to reconnect and merge current changes back during
         mx_check_mailbox()
      */
      mutt_error _("A fatal error occurred.  Will attempt reconnection.");
    }
    return;
  }

  idata->status = IMAP_FATAL;

  if ((idata->state >= IMAP_SELECTED) &&
      (idata->reopen & IMAP_REOPEN_ALLOW))
  {
    mx_fastclose_mailbox (idata->ctx);
    mutt_socket_close (idata->conn);
    mutt_error (_("Mailbox %s@%s closed"),
                idata->conn->account.login, idata->conn->account.host);
    mutt_sleep (1);
    idata->state = IMAP_DISCONNECTED;
  }

  if (idata->state < IMAP_SELECTED)
    imap_close_connection (idata);
}

/* cmd_handle_untagged: fallback parser for otherwise unhandled messages. */
static int cmd_handle_untagged (IMAP_DATA* idata)
{
  char* s;
  char* pn;
  unsigned int count;

  s = imap_next_word (idata->buf);
  pn = imap_next_word (s);

  if ((idata->state >= IMAP_SELECTED) && isdigit ((unsigned char) *s))
  {
    pn = s;
    s = imap_next_word (s);

    /* EXISTS and EXPUNGE are always related to the SELECTED mailbox for the
     * connection, so update that one.
     */
    if (ascii_strncasecmp ("EXISTS", s, 6) == 0)
    {
      dprint (2, (debugfile, "Handling EXISTS\n"));

      /* new mail arrived */
      if (mutt_atoui (pn, &count, MUTT_ATOI_ALLOW_TRAILING) < 0)
        return 0;

      if (count < idata->max_msn)
      {
        /* Notes 6.0.3 has a tendency to report fewer messages exist than
         * it should. */
	dprint (1, (debugfile, "Message count is out of sync"));
	return 0;
      }
      /* at least the InterChange server sends EXISTS messages freely,
       * even when there is no new mail */
      else if (count == idata->max_msn)
	dprint (3, (debugfile,
                    "cmd_handle_untagged: superfluous EXISTS message.\n"));
      else
      {
        dprint (2, (debugfile,
                    "cmd_handle_untagged: New mail in %s - %d messages total.\n",
                    idata->mailbox, count));
        idata->reopen |= IMAP_NEWMAIL_PENDING;
	idata->newMailCount = count;
      }
    }
    /* pn vs. s: need initial seqno */
    else if (ascii_strncasecmp ("EXPUNGE", s, 7) == 0)
      cmd_parse_expunge (idata, pn);
    else if (ascii_strncasecmp ("FETCH", s, 5) == 0)
      cmd_parse_fetch (idata, pn);
  }
  else if ((idata->state >= IMAP_SELECTED) &&
           ascii_strncasecmp ("VANISHED", s, 8) == 0)
    cmd_parse_vanished (idata, pn);
  else if (ascii_strncasecmp ("CAPABILITY", s, 10) == 0)
    cmd_parse_capability (idata, s);
  else if (!ascii_strncasecmp ("OK [CAPABILITY", s, 14))
    cmd_parse_capability (idata, pn);
  else if (!ascii_strncasecmp ("OK [CAPABILITY", pn, 14))
    cmd_parse_capability (idata, imap_next_word (pn));
  else if (ascii_strncasecmp ("LIST", s, 4) == 0)
    cmd_parse_list (idata, s);
  else if (ascii_strncasecmp ("LSUB", s, 4) == 0)
    cmd_parse_lsub (idata, s);
  else if (ascii_strncasecmp ("MYRIGHTS", s, 8) == 0)
    cmd_parse_myrights (idata, s);
  else if (ascii_strncasecmp ("SEARCH", s, 6) == 0)
    cmd_parse_search (idata, s);
  else if (ascii_strncasecmp ("STATUS", s, 6) == 0)
    cmd_parse_status (idata, s);
  else if (ascii_strncasecmp ("ENABLED", s, 7) == 0)
    cmd_parse_enabled (idata, s);
  else if (ascii_strncasecmp ("BYE", s, 3) == 0)
  {
    dprint (2, (debugfile, "Handling BYE\n"));

    /* check if we're logging out */
    if (idata->status == IMAP_BYE)
      return 0;

    /* server shut down our connection */
    s += 3;
    SKIPWS (s);
    mutt_error ("%s", s);
    mutt_sleep (2);
    cmd_handle_fatal (idata);

    return -1;
  }
  else if (option (OPTIMAPSERVERNOISE) && (ascii_strncasecmp ("NO", s, 2) == 0))
  {
    dprint (2, (debugfile, "Handling untagged NO\n"));

    /* Display the warning message from the server */
    mutt_error ("%s", s+2);
    mutt_sleep (2);
  }

  return 0;
}

/* cmd_parse_capabilities: set capability bits according to CAPABILITY
 *   response */
static void cmd_parse_capability (IMAP_DATA* idata, char* s)
{
  int x;
  char* bracket;

  dprint (3, (debugfile, "Handling CAPABILITY\n"));

  s = imap_next_word (s);
  if ((bracket = strchr (s, ']')))
    *bracket = '\0';
  FREE(&idata->capstr);
  idata->capstr = safe_strdup (s);

  memset (idata->capabilities, 0, sizeof (idata->capabilities));

  while (*s)
  {
    for (x = 0; x < CAPMAX; x++)
      if (imap_wordcasecmp(Capabilities[x], s) == 0)
      {
	mutt_bit_set (idata->capabilities, x);
	break;
      }
    s = imap_next_word (s);
  }
}

/* cmd_parse_expunge: mark headers with new sequence ID and mark idata to
 *   be reopened at our earliest convenience */
static void cmd_parse_expunge (IMAP_DATA* idata, const char* s)
{
  unsigned int exp_msn, cur;
  HEADER* h;

  dprint (2, (debugfile, "Handling EXPUNGE\n"));

  if (mutt_atoui (s, &exp_msn, MUTT_ATOI_ALLOW_TRAILING) < 0 ||
      exp_msn < 1 || exp_msn > idata->max_msn)
    return;

  h = idata->msn_index[exp_msn - 1];
  if (h)
  {
    /* imap_expunge_mailbox() will rewrite h->index.
     * It needs to resort using SORT_ORDER anyway, so setting to INT_MAX
     * makes the code simpler and possibly more efficient. */
    h->index = INT_MAX;
    HEADER_DATA(h)->msn = 0;
  }

  /* decrement seqno of those above. */
  for (cur = exp_msn; cur < idata->max_msn; cur++)
  {
    h = idata->msn_index[cur];
    if (h)
      HEADER_DATA(h)->msn--;
    idata->msn_index[cur - 1] = h;
  }

  idata->msn_index[idata->max_msn - 1] = NULL;
  idata->max_msn--;

  idata->reopen |= IMAP_EXPUNGE_PENDING;
}

/* cmd_parse_vanished: handles VANISHED (RFC 7162), which is like
 *   expunge, but passes a seqset of UIDs.  An optional (EARLIER) argument
 *   specifies not to decrement subsequent MSNs. */
static void cmd_parse_vanished (IMAP_DATA* idata, char* s)
{
  int earlier = 0, rc;
  char *end_of_seqset;
  SEQSET_ITERATOR *iter;
  unsigned int uid, exp_msn, cur;
  HEADER* h;

  dprint (2, (debugfile, "Handling VANISHED\n"));

  if (ascii_strncasecmp ("(EARLIER)", s, 9) == 0)
  {
    /* The RFC says we should not decrement msns with the VANISHED EARLIER tag.
     * My experimentation says that's crap. */
    /* earlier = 1; */
    s = imap_next_word (s);
  }

  end_of_seqset = s;
  while (*end_of_seqset)
  {
    if (!strchr ("0123456789:,", *end_of_seqset))
      *end_of_seqset = '\0';
    else
      end_of_seqset++;
  }

  iter = mutt_seqset_iterator_new (s);
  if (!iter)
  {
    dprint (2, (debugfile, "VANISHED: empty seqset [%s]?\n", s));
    return;
  }

  while ((rc = mutt_seqset_iterator_next (iter, &uid)) == 0)
  {
    h = (HEADER *)int_hash_find (idata->uid_hash, uid);
    if (!h)
      continue;

    exp_msn = HEADER_DATA(h)->msn;

    /* imap_expunge_mailbox() will rewrite h->index.
     * It needs to resort using SORT_ORDER anyway, so setting to INT_MAX
     * makes the code simpler and possibly more efficient. */
    h->index = INT_MAX;
    HEADER_DATA(h)->msn = 0;

    if (exp_msn < 1 || exp_msn > idata->max_msn)
    {
      dprint (1, (debugfile,
                  "VANISHED: msn for UID %u is incorrect.\n", uid));
      continue;
    }
    if (idata->msn_index[exp_msn - 1] != h)
    {
      dprint (1, (debugfile,
                  "VANISHED: msn_index for UID %u is incorrect.\n", uid));
      continue;
    }

    idata->msn_index[exp_msn - 1] = NULL;

    if (!earlier)
    {
      /* decrement seqno of those above. */
      for (cur = exp_msn; cur < idata->max_msn; cur++)
      {
        h = idata->msn_index[cur];
        if (h)
          HEADER_DATA(h)->msn--;
        idata->msn_index[cur - 1] = h;
      }

      idata->msn_index[idata->max_msn - 1] = NULL;
      idata->max_msn--;
    }
  }

  if (rc < 0)
    dprint (1, (debugfile, "VANISHED: illegal seqset %s\n", s));

  idata->reopen |= IMAP_EXPUNGE_PENDING;

  mutt_seqset_iterator_free (&iter);
}

/* cmd_parse_fetch: Load fetch response into IMAP_DATA. Currently only
 *   handles unanticipated FETCH responses, and only FLAGS data. We get
 *   these if another client has changed flags for a mailbox we've selected.
 *   Of course, a lot of code here duplicates code in message.c. */
static void cmd_parse_fetch (IMAP_DATA* idata, char* s)
{
  unsigned int msn, uid;
  HEADER *h;
  char *flags = NULL;
  int uid_checked = 0;
  int server_changes = 0;

  dprint (3, (debugfile, "Handling FETCH\n"));

  if (mutt_atoui (s, &msn, MUTT_ATOI_ALLOW_TRAILING) < 0)
  {
    dprint (3, (debugfile, "cmd_parse_fetch: Skipping FETCH response - illegal MSN\n"));
    return;
  }

  if (msn < 1 || msn > idata->max_msn)
  {
    dprint (3, (debugfile,
                "cmd_parse_fetch: Skipping FETCH response - MSN %u out of range\n",
                msn));
    return;
  }

  h = idata->msn_index[msn - 1];
  if (!h || !h->active)
  {
    dprint (3, (debugfile,
                "cmd_parse_fetch: Skipping FETCH response - MSN %u not in msn_index\n",
                msn));
    return;
  }

  dprint (2, (debugfile, "Message UID %u updated\n", HEADER_DATA(h)->uid));
  /* skip FETCH */
  s = imap_next_word (s);
  s = imap_next_word (s);

  if (*s != '(')
  {
    dprint (1, (debugfile, "Malformed FETCH response"));
    return;
  }
  s++;

  while (*s)
  {
    SKIPWS (s);

    if (ascii_strncasecmp ("FLAGS", s, 5) == 0)
    {
      flags = s;
      if (uid_checked)
        break;

      s += 5;
      SKIPWS(s);
      if (*s != '(')
      {
        dprint (1, (debugfile, "cmd_parse_fetch: bogus FLAGS response: %s\n",
                    s));
        return;
      }
      s++;
      while (*s && *s != ')')
        s++;
      if (*s == ')')
        s++;
      else
      {
        dprint (1, (debugfile,
                    "cmd_parse_fetch: Unterminated FLAGS response: %s\n", s));
        return;
      }
    }
    else if (ascii_strncasecmp ("UID", s, 3) == 0)
    {
      s += 3;
      SKIPWS (s);
      if (mutt_atoui (s, &uid, MUTT_ATOI_ALLOW_TRAILING) < 0)
      {
        dprint (1, (debugfile, "cmd_parse_fetch: Illegal UID.  Skipping update.\n"));
        return;
      }
      if (uid != HEADER_DATA(h)->uid)
      {
        dprint (1, (debugfile, "cmd_parse_fetch: UID vs MSN mismatch.  Skipping update.\n"));
        return;
      }
      uid_checked = 1;
      if (flags)
        break;
      s = imap_next_word (s);
    }
    else if (ascii_strncasecmp ("MODSEQ", s, 6) == 0)
    {
      s += 6;
      SKIPWS(s);
      if (*s != '(')
      {
        dprint (1, (debugfile, "cmd_parse_fetch: bogus MODSEQ response: %s\n",
                    s));
        return;
      }
      s++;
      while (*s && *s != ')')
        s++;
      if (*s == ')')
        s++;
      else
      {
        dprint (1, (debugfile,
                    "cmd_parse_fetch: Unterminated MODSEQ response: %s\n", s));
        return;
      }
    }
    else if (*s == ')')
      break; /* end of request */
    else if (*s)
    {
      dprint (2, (debugfile, "Only handle FLAGS updates\n"));
      break;
    }
  }

  if (flags)
  {
    imap_set_flags (idata, h, flags, &server_changes);
    if (server_changes)
    {
      /* If server flags could conflict with mutt's flags, reopen the mailbox. */
      if (h->changed)
        idata->reopen |= IMAP_EXPUNGE_PENDING;
      else
        idata->check_status |= IMAP_FLAGS_PENDING;
    }
  }
}

static void cmd_parse_list (IMAP_DATA* idata, char* s)
{
  IMAP_LIST* list;
  IMAP_LIST lb;
  char delimbuf[5]; /* worst case: "\\"\0 */
  unsigned int litlen;

  if (idata->cmddata && idata->cmdtype == IMAP_CT_LIST)
    list = (IMAP_LIST*)idata->cmddata;
  else
    list = &lb;

  memset (list, 0, sizeof (IMAP_LIST));

  /* flags */
  s = imap_next_word (s);
  if (*s != '(')
  {
    dprint (1, (debugfile, "Bad LIST response\n"));
    return;
  }
  s++;
  while (*s)
  {
    if (!ascii_strncasecmp (s, "\\NoSelect", 9))
      list->noselect = 1;
    else if (!ascii_strncasecmp (s, "\\NonExistent", 12)) /* rfc5258 */
      list->noselect = 1;
    else if (!ascii_strncasecmp (s, "\\NoInferiors", 12))
      list->noinferiors = 1;
    else if (!ascii_strncasecmp (s, "\\HasNoChildren", 14)) /* rfc5258*/
      list->noinferiors = 1;

    s = imap_next_word (s);
    if (*(s - 2) == ')')
      break;
  }

  /* Delimiter */
  if (ascii_strncasecmp (s, "NIL", 3))
  {
    delimbuf[0] = '\0';
    safe_strcat (delimbuf, 5, s);
    imap_unquote_string (delimbuf);
    list->delim = delimbuf[0];
  }

  /* Name */
  s = imap_next_word (s);
  /* Notes often responds with literals here. We need a real tokenizer. */
  if (!imap_get_literal_count (s, &litlen))
  {
    if (imap_cmd_step (idata) != IMAP_CMD_CONTINUE)
    {
      idata->status = IMAP_FATAL;
      return;
    }

    if (strlen(idata->buf) < litlen)
    {
      dprint (1, (debugfile, "Error parsing LIST mailbox\n"));
      return;
    }

    list->name = idata->buf;
    s = list->name + litlen;
    if (*s)
    {
      *s = '\0';
      s++;
      SKIPWS(s);
    }
  }
  else
  {
    list->name = s;
    /* Exclude rfc5258 RECURSIVEMATCH CHILDINFO suffix */
    s = imap_next_word (s);
    if (*s)
      *(s - 1) = '\0';
    imap_unmunge_mbox_name (idata, list->name);
  }

  if (list->name[0] == '\0')
  {
    idata->delim = list->delim;
    dprint (3, (debugfile, "Root delimiter: %c\n", idata->delim));
  }
}

static void cmd_parse_lsub (IMAP_DATA* idata, char* s)
{
  BUFFER *mailbox = NULL;
  ciss_url_t url;
  IMAP_LIST list;

  if (idata->cmddata && idata->cmdtype == IMAP_CT_LIST)
  {
    /* caller will handle response itself */
    cmd_parse_list (idata, s);
    return;
  }

  if (!option (OPTIMAPCHECKSUBSCRIBED))
    return;

  idata->cmdtype = IMAP_CT_LIST;
  idata->cmddata = &list;
  cmd_parse_list (idata, s);
  idata->cmddata = NULL;
  /* noselect is for a gmail quirk (#3445) */
  if (!list.name || list.noselect)
    return;

  dprint (3, (debugfile, "Subscribing to %s\n", list.name));

  mutt_account_tourl (&idata->conn->account, &url, 0);
  url.path = list.name;

  mailbox = mutt_buffer_pool_get ();
  /* Include password if it was part of the connection URL.
   * This allows full connection using just the buffy url.
   * It also helps with browser sticky-cursor and sidebar comparison,
   * since the context->path will include the password too. */
  url_ciss_tobuffer (&url, mailbox, U_DECODE_PASSWD);

  mutt_buffy_add (mutt_b2s (mailbox), NULL, -1, -1);
  mutt_buffer_pool_release (&mailbox);
}

/* cmd_parse_myrights: set rights bits according to MYRIGHTS response */
static void cmd_parse_myrights (IMAP_DATA* idata, const char* s)
{
  dprint (2, (debugfile, "Handling MYRIGHTS\n"));

  s = imap_next_word ((char*)s);
  s = imap_next_word ((char*)s);

  /* zero out current rights set */
  memset (idata->ctx->rights, 0, sizeof (idata->ctx->rights));

  while (*s && !isspace((unsigned char) *s))
  {
    switch (*s)
    {
      case 'l':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_LOOKUP);
	break;
      case 'r':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_READ);
	break;
      case 's':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_SEEN);
	break;
      case 'w':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_WRITE);
	break;
      case 'i':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_INSERT);
	break;
      case 'p':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_POST);
	break;
      case 'a':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_ADMIN);
	break;
      case 'k':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_CREATE);
        break;
      case 'x':
        mutt_bit_set (idata->ctx->rights, MUTT_ACL_DELMX);
        break;
      case 't':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_DELETE);
        break;
      case 'e':
        mutt_bit_set (idata->ctx->rights, MUTT_ACL_EXPUNGE);
        break;

        /* obsolete rights */
      case 'c':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_CREATE);
        mutt_bit_set (idata->ctx->rights, MUTT_ACL_DELMX);
	break;
      case 'd':
	mutt_bit_set (idata->ctx->rights, MUTT_ACL_DELETE);
        mutt_bit_set (idata->ctx->rights, MUTT_ACL_EXPUNGE);
	break;
      default:
        dprint(1, (debugfile, "Unknown right: %c\n", *s));
    }
    s++;
  }
}

/* cmd_parse_search: store SEARCH response for later use */
static void cmd_parse_search (IMAP_DATA* idata, const char* s)
{
  unsigned int uid;
  HEADER *h;

  dprint (2, (debugfile, "Handling SEARCH\n"));

  while ((s = imap_next_word ((char*)s)) && *s != '\0')
  {
    if (mutt_atoui (s, &uid, MUTT_ATOI_ALLOW_TRAILING) < 0)
      continue;
    h = (HEADER *)int_hash_find (idata->uid_hash, uid);
    if (h)
      h->matched = 1;
  }
}

/* first cut: just do buffy update. Later we may wish to cache all
 * mailbox information, even that not desired by buffy */
static void cmd_parse_status (IMAP_DATA* idata, char* s)
{
  char* mailbox;
  char* value;
  BUFFY* inc;
  IMAP_MBOX mx;
  unsigned long ulcount;
  unsigned int count;
  IMAP_STATUS *status;
  unsigned int olduv, oldun;
  unsigned int litlen;
  short new = 0;
  short new_msg_count = 0;

  mailbox = imap_next_word (s);

  /* We need a real tokenizer. */
  if (!imap_get_literal_count (mailbox, &litlen))
  {
    if (imap_cmd_step (idata) != IMAP_CMD_CONTINUE)
    {
      idata->status = IMAP_FATAL;
      return;
    }

    if (strlen(idata->buf) < litlen)
    {
      dprint (1, (debugfile, "Error parsing STATUS mailbox\n"));
      return;
    }

    mailbox = idata->buf;
    s = mailbox + litlen;
    *s = '\0';
    s++;
    SKIPWS(s);
  }
  else
  {
    s = imap_next_word (mailbox);
    *(s - 1) = '\0';
    imap_unmunge_mbox_name (idata, mailbox);
  }

  status = imap_mboxcache_get (idata, mailbox, 1);
  olduv = status->uidvalidity;
  oldun = status->uidnext;

  if (*s++ != '(')
  {
    dprint (1, (debugfile, "Error parsing STATUS\n"));
    return;
  }
  while (*s && *s != ')')
  {
    value = imap_next_word (s);

    errno = 0;
    ulcount = strtoul (value, &value, 10);
    if ((errno == ERANGE && ulcount == ULONG_MAX) ||
        ((unsigned int) ulcount != ulcount))
    {
      dprint (1, (debugfile, "Error parsing STATUS number\n"));
      return;
    }
    count = (unsigned int) ulcount;

    if (!ascii_strncmp ("MESSAGES", s, 8))
    {
      status->messages = count;
      new_msg_count = 1;
    }
    else if (!ascii_strncmp ("RECENT", s, 6))
      status->recent = count;
    else if (!ascii_strncmp ("UIDNEXT", s, 7))
      status->uidnext = count;
    else if (!ascii_strncmp ("UIDVALIDITY", s, 11))
      status->uidvalidity = count;
    else if (!ascii_strncmp ("UNSEEN", s, 6))
      status->unseen = count;

    s = value;
    if (*s && *s != ')')
      s = imap_next_word (s);
  }
  dprint (3, (debugfile, "%s (UIDVALIDITY: %u, UIDNEXT: %u) %d messages, %d recent, %d unseen\n",
              status->name, status->uidvalidity, status->uidnext,
              status->messages, status->recent, status->unseen));

  /* caller is prepared to handle the result herself */
  if (idata->cmddata && idata->cmdtype == IMAP_CT_STATUS)
  {
    memcpy (idata->cmddata, status, sizeof (IMAP_STATUS));
    return;
  }

  dprint (3, (debugfile, "Running default STATUS handler\n"));

  /* should perhaps move this code back to imap_buffy_check */
  for (inc = Incoming; inc; inc = inc->next)
  {
    if (inc->magic != MUTT_IMAP)
      continue;

    if (imap_parse_path (mutt_b2s (inc->pathbuf), &mx) < 0)
    {
      dprint (1, (debugfile, "Error parsing mailbox %s, skipping\n", mutt_b2s (inc->pathbuf)));
      continue;
    }
    /* dprint (2, (debugfile, "Buffy entry: [%s] mbox: [%s]\n", inc->path, NONULL(mx.mbox))); */

    if (imap_account_match (&idata->conn->account, &mx.account))
    {
      if (mx.mbox)
      {
	value = safe_strdup (mx.mbox);
	imap_fix_path (idata, mx.mbox, value, mutt_strlen (value) + 1);
	FREE (&mx.mbox);
      }
      else
	value = safe_strdup ("INBOX");

      if (value && !imap_mxcmp (mailbox, value))
      {
        dprint (3, (debugfile, "Found %s in buffy list (OV: %u ON: %u U: %d)\n",
                    mailbox, olduv, oldun, status->unseen));

	if (option(OPTMAILCHECKRECENT))
	{
	  if (olduv && olduv == status->uidvalidity)
	  {
	    if (oldun < status->uidnext)
	      new = (status->unseen > 0);
	  }
	  else if (!olduv && !oldun)
	    /* first check per session, use recent. might need a flag for this. */
	    new = (status->recent > 0);
	  else
	    new = (status->unseen > 0);
	}
	else
          new = (status->unseen > 0);

#ifdef USE_SIDEBAR
        if ((inc->new != new) ||
            (inc->msg_count != status->messages) ||
            (inc->msg_unread != status->unseen))
          mutt_set_current_menu_redraw (REDRAW_SIDEBAR);
#endif
        inc->new = new;
        if (new_msg_count)
          inc->msg_count = status->messages;
        inc->msg_unread = status->unseen;

	if (inc->new)
	  /* force back to keep detecting new mail until the mailbox is
	     opened */
	  status->uidnext = oldun;

        FREE (&value);
        return;
      }

      FREE (&value);
    }

    FREE (&mx.mbox);
  }
}

/* cmd_parse_enabled: record what the server has enabled */
static void cmd_parse_enabled (IMAP_DATA* idata, const char* s)
{
  dprint (2, (debugfile, "Handling ENABLED\n"));

  while ((s = imap_next_word ((char*)s)) && *s != '\0')
  {
    if (ascii_strncasecmp(s, "UTF8=ACCEPT", 11) == 0 ||
        ascii_strncasecmp(s, "UTF8=ONLY", 9) == 0)
      idata->unicode = 1;
    if (ascii_strncasecmp(s, "QRESYNC", 7) == 0)
      idata->qresync = 1;
  }
}
