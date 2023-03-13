/* mutt - text oriented MIME mail user agent
 * Copyright (C) 2002 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 2005-2009 Brendan Cully <brendan@kublai.com>
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
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */

/* This file contains code for direct SMTP delivery of email messages. */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_socket.h"
#ifdef USE_SSL
# include "mutt_ssl.h"
#endif
#ifdef USE_SASL_CYRUS
#include "mutt_sasl.h"

#include <sasl/sasl.h>
#include <sasl/saslutil.h>
#endif
#ifdef USE_SASL_GNU
#include "mutt_sasl_gnu.h"
#include <gsasl.h>
#endif

#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/stat.h>

#define smtp_success(x) ((x)/100 == 2)
#define smtp_ready 334
#define smtp_continue 354

#define smtp_err_read -2
#define smtp_err_write -3
#define smtp_err_code -4

#define SMTP_PORT 25
#define SMTPS_PORT 465

#define SMTP_AUTH_SUCCESS 0
#define SMTP_AUTH_UNAVAIL 1
#define SMTP_AUTH_FAIL    -1

enum {
  STARTTLS,
  AUTH,
  DSN,
  EIGHTBITMIME,
  SMTPUTF8,

  CAPMAX
};

static int smtp_auth (CONNECTION* conn);
static int smtp_auth_oauth (CONNECTION* conn, int xoauth2);
#ifdef USE_SASL_CYRUS
static int smtp_auth_sasl (CONNECTION* conn, const char* mechanisms);
#endif
#ifdef USE_SASL_GNU
static int smtp_auth_gsasl (CONNECTION* conn, const char* method);
#endif

static int smtp_fill_account (ACCOUNT* account);
static int smtp_open (CONNECTION* conn);

static int Esmtp = 0;
static char* AuthMechs = NULL;
static unsigned char Capabilities[(CAPMAX + 7)/ 8];

/* Note: the 'len' parameter is actually the number of bytes, as
 * returned by mutt_socket_readln().  If all callers are converted to
 * mutt_socket_buffer_readln() we can pass in the actual len, or
 * perhaps the buffer itself.
 */
static int smtp_code (const char *buf, size_t len, int *n)
{
  char code[4];

  if (len < 4)
    return -1;
  code[0] = buf[0];
  code[1] = buf[1];
  code[2] = buf[2];
  code[3] = 0;
  if (mutt_atoi (code, n, 0) < 0)
    return -1;
  return 0;
}

/* Reads a command response from the SMTP server.
 * Returns:
 * 0	on success (2xx code) or continue (354 code)
 * -1	write error, or any other response code
 */
static int
smtp_get_resp (CONNECTION * conn)
{
  int n;
  char buf[1024];
  char *smtp_response;

  do
  {
    n = mutt_socket_readln (buf, sizeof (buf), conn);
    if (n < 4)
    {
      /* read error, or no response code */
      return smtp_err_read;
    }

    smtp_response = buf + 3;
    if (*smtp_response)
    {
      smtp_response++;

      if (!ascii_strncasecmp ("8BITMIME", smtp_response, 8))
        mutt_bit_set (Capabilities, EIGHTBITMIME);
      else if (!ascii_strncasecmp ("AUTH ", smtp_response, 5))
      {
        mutt_bit_set (Capabilities, AUTH);
        FREE (&AuthMechs);
        AuthMechs = safe_strdup (smtp_response + 5);
      }
      else if (!ascii_strncasecmp ("DSN", smtp_response, 3))
        mutt_bit_set (Capabilities, DSN);
      else if (!ascii_strncasecmp ("STARTTLS", smtp_response, 8))
        mutt_bit_set (Capabilities, STARTTLS);
      else if (!ascii_strncasecmp ("SMTPUTF8", smtp_response, 8))
        mutt_bit_set (Capabilities, SMTPUTF8);
    }

    if (smtp_code (buf, n, &n) < 0)
      return smtp_err_code;

  } while (buf[3] == '-');

  if (smtp_success (n) || n == smtp_continue)
    return 0;

  mutt_error (_("SMTP session failed: %s"), buf);
  return -1;
}

static int
smtp_get_auth_response (CONNECTION *conn, BUFFER *input_buf, int *smtp_rc,
                        BUFFER *response_buf)
{
  const char *smtp_response;

  mutt_buffer_clear (response_buf);
  do
  {
    if (mutt_socket_buffer_readln (input_buf, conn) < 0)
      return -1;
    if (smtp_code (mutt_b2s (input_buf),
                   mutt_buffer_len (input_buf) + 1, /* number of bytes */
                   smtp_rc) < 0)
      return -1;

    if (*smtp_rc != smtp_ready)
      break;

    smtp_response = mutt_b2s (input_buf) + 3;
    if (*smtp_response)
    {
      smtp_response++;
      mutt_buffer_addstr (response_buf, smtp_response);
    }
  } while (mutt_b2s (input_buf)[3] == '-');

  return 0;
}

static int
smtp_rcpt_to (CONNECTION * conn, const ADDRESS * a)
{
  char buf[1024];
  int r;

  while (a)
  {
    /* weed out group mailboxes, since those are for display only */
    if (!a->mailbox || a->group)
    {
      a = a->next;
      continue;
    }
    if (mutt_bit_isset (Capabilities, DSN) && DsnNotify)
      snprintf (buf, sizeof (buf), "RCPT TO:<%s> NOTIFY=%s\r\n",
                a->mailbox, DsnNotify);
    else
      snprintf (buf, sizeof (buf), "RCPT TO:<%s>\r\n", a->mailbox);
    if (mutt_socket_write (conn, buf) == -1)
      return smtp_err_write;
    if ((r = smtp_get_resp (conn)))
      return r;
    a = a->next;
  }

  return 0;
}

static int
smtp_data (CONNECTION * conn, const char *msgfile)
{
  char buf[1024];
  FILE *fp = 0;
  progress_t progress;
  struct stat st;
  int r, term = 0;
  size_t buflen = 0;

  fp = fopen (msgfile, "r");
  if (!fp)
  {
    mutt_error (_("SMTP session failed: unable to open %s"), msgfile);
    return -1;
  }
  stat (msgfile, &st);
  unlink (msgfile);
  mutt_progress_init (&progress, _("Sending message..."), MUTT_PROGRESS_SIZE,
                      NetInc, st.st_size);

  snprintf (buf, sizeof (buf), "DATA\r\n");
  if (mutt_socket_write (conn, buf) == -1)
  {
    safe_fclose (&fp);
    return smtp_err_write;
  }
  if ((r = smtp_get_resp (conn)))
  {
    safe_fclose (&fp);
    return r;
  }

  while (fgets (buf, sizeof (buf) - 1, fp))
  {
    buflen = mutt_strlen (buf);
    term = buflen && buf[buflen-1] == '\n';
    if (term && (buflen == 1 || buf[buflen - 2] != '\r'))
      snprintf (buf + buflen - 1, sizeof (buf) - buflen + 1, "\r\n");
    if (buf[0] == '.')
    {
      if (mutt_socket_write_d (conn, ".", -1, MUTT_SOCK_LOG_FULL) == -1)
      {
        safe_fclose (&fp);
        return smtp_err_write;
      }
    }
    if (mutt_socket_write_d (conn, buf, -1, MUTT_SOCK_LOG_FULL) == -1)
    {
      safe_fclose (&fp);
      return smtp_err_write;
    }
    mutt_progress_update (&progress, ftell (fp), -1);
  }
  if (!term && buflen &&
      mutt_socket_write_d (conn, "\r\n", -1, MUTT_SOCK_LOG_FULL) == -1)
  {
    safe_fclose (&fp);
    return smtp_err_write;
  }
  safe_fclose (&fp);

  /* terminate the message body */
  if (mutt_socket_write (conn, ".\r\n") == -1)
    return smtp_err_write;

  if ((r = smtp_get_resp (conn)))
    return r;

  return 0;
}


/* Returns 1 if a contains at least one 8-bit character, 0 if none do.
 */
static int address_uses_unicode(const char *a)
{
  if (!a)
    return 0;

  while (*a)
  {
    if ((unsigned char) *a & (1<<7))
      return 1;
    a++;
  }

  return 0;
}


/* Returns 1 if any address in a contains at least one 8-bit
 * character, 0 if none do.
 */
static int addresses_use_unicode(const ADDRESS* a)
{
  while (a)
  {
    if (a->mailbox && !a->group && address_uses_unicode(a->mailbox))
      return 1;
    a = a->next;
  }
  return 0;
}


int
mutt_smtp_send (const ADDRESS* from, const ADDRESS* to, const ADDRESS* cc,
                const ADDRESS* bcc, const char *msgfile, int eightbit)
{
  CONNECTION *conn;
  ACCOUNT account;
  const char* envfrom;
  char buf[1024];
  int ret = -1;

  /* it might be better to synthesize an envelope from from user and host
   * but this condition is most likely arrived at accidentally */
  if (EnvFrom)
    envfrom = EnvFrom->mailbox;
  else if (from)
    envfrom = from->mailbox;
  else
  {
    mutt_error (_("No from address given"));
    return -1;
  }

  if (smtp_fill_account (&account) < 0)
    return ret;

  if (!(conn = mutt_conn_find (NULL, &account)))
    return -1;

  Esmtp = eightbit;

  do
  {
    /* send our greeting */
    if (( ret = smtp_open (conn)))
      break;
    FREE (&AuthMechs);

    /* send the sender's address */
    ret = snprintf (buf, sizeof (buf), "MAIL FROM:<%s>", envfrom);
    if (eightbit && mutt_bit_isset (Capabilities, EIGHTBITMIME))
    {
      safe_strncat (buf, sizeof (buf), " BODY=8BITMIME", 15);
      ret += 14;
    }
    if (DsnReturn && mutt_bit_isset (Capabilities, DSN))
      ret += snprintf (buf + ret, sizeof (buf) - ret, " RET=%s", DsnReturn);
    if (mutt_bit_isset (Capabilities, SMTPUTF8) &&
	(address_uses_unicode(envfrom) ||
	 addresses_use_unicode(to) ||
	 addresses_use_unicode(cc) ||
	 addresses_use_unicode(bcc)))
      ret += snprintf (buf + ret, sizeof (buf) - ret, " SMTPUTF8");
    safe_strncat (buf, sizeof (buf), "\r\n", 3);
    if (mutt_socket_write (conn, buf) == -1)
    {
      ret = smtp_err_write;
      break;
    }
    if ((ret = smtp_get_resp (conn)))
      break;

    /* send the recipient list */
    if ((ret = smtp_rcpt_to (conn, to)) || (ret = smtp_rcpt_to (conn, cc))
        || (ret = smtp_rcpt_to (conn, bcc)))
      break;

    /* send the message data */
    if ((ret = smtp_data (conn, msgfile)))
      break;

    mutt_socket_write (conn, "QUIT\r\n");

    ret = 0;
  }
  while (0);

  if (conn)
    mutt_socket_close (conn);

  if (ret == smtp_err_read)
    mutt_error (_("SMTP session failed: read error"));
  else if (ret == smtp_err_write)
    mutt_error (_("SMTP session failed: write error"));
  else if (ret == smtp_err_code)
    mutt_error (_("Invalid server response"));

  return ret;
}

static int smtp_fill_account (ACCOUNT* account)
{
  static unsigned short SmtpPort = 0;

  struct servent* service;
  ciss_url_t url;
  char* urlstr;

  account->flags = 0;
  account->port = 0;
  account->type = MUTT_ACCT_TYPE_SMTP;

  urlstr = safe_strdup (SmtpUrl);
  url_parse_ciss (&url, urlstr);
  if ((url.scheme != U_SMTP && url.scheme != U_SMTPS)
      || mutt_account_fromurl (account, &url) < 0)
  {
    FREE (&urlstr);
    mutt_error (_("Invalid SMTP URL: %s"), SmtpUrl);
    mutt_sleep (1);
    return -1;
  }
  FREE (&urlstr);

  if (url.scheme == U_SMTPS)
    account->flags |= MUTT_ACCT_SSL;

  if (!account->port)
  {
    if (account->flags & MUTT_ACCT_SSL)
      account->port = SMTPS_PORT;
    else
    {
      if (!SmtpPort)
      {
        service = getservbyname ("smtp", "tcp");
        if (service)
          SmtpPort = ntohs (service->s_port);
        else
          SmtpPort = SMTP_PORT;
        dprint (3, (debugfile, "Using default SMTP port %d\n", SmtpPort));
      }
      account->port = SmtpPort;
    }
  }

  return 0;
}

static int smtp_helo (CONNECTION* conn)
{
  char buf[LONG_STRING];
  const char* fqdn;

  memset (Capabilities, 0, sizeof (Capabilities));

  if (!Esmtp)
  {
    /* if TLS or AUTH are requested, use EHLO */
    if (conn->account.flags & MUTT_ACCT_USER)
      Esmtp = 1;
#ifdef USE_SSL
    if (option (OPTSSLFORCETLS) || quadoption (OPT_SSLSTARTTLS) != MUTT_NO)
      Esmtp = 1;
#endif
  }

  if (!(fqdn = mutt_fqdn (0)))
    fqdn = NONULL (Hostname);

  snprintf (buf, sizeof (buf), "%s %s\r\n", Esmtp ? "EHLO" : "HELO", fqdn);
  /* XXX there should probably be a wrapper in mutt_socket.c that
   * repeatedly calls conn->write until all data is sent.  This
   * currently doesn't check for a short write.
   */
  if (mutt_socket_write (conn, buf) == -1)
    return smtp_err_write;
  return smtp_get_resp (conn);
}

static int smtp_open (CONNECTION* conn)
{
  int rc;

  if (mutt_socket_open (conn))
    return -1;

  /* get greeting string */
  if ((rc = smtp_get_resp (conn)))
    return rc;

  if ((rc = smtp_helo (conn)))
    return rc;

#ifdef USE_SSL
  if (conn->ssf)
    rc = MUTT_NO;
  else if (option (OPTSSLFORCETLS))
    rc = MUTT_YES;
  else if (mutt_bit_isset (Capabilities, STARTTLS) &&
           (rc = query_quadoption (OPT_SSLSTARTTLS,
                                   _("Secure connection with TLS?"))) == -1)
    return rc;

  if (rc == MUTT_YES)
  {
    if (mutt_socket_write (conn, "STARTTLS\r\n") < 0)
      return smtp_err_write;
    if ((rc = smtp_get_resp (conn)))
      return rc;

    if (mutt_ssl_starttls (conn))
    {
      mutt_error (_("Could not negotiate TLS connection"));
      mutt_sleep (1);
      return -1;
    }

    /* re-EHLO to get authentication mechanisms */
    if ((rc = smtp_helo (conn)))
      return rc;
  }
#endif

  /* In some cases, the SMTP server will advertise AUTH even though
   * it's not required.  Check if the username is explicitly in the
   * URL to decide whether to call smtp_auth().
   *
   * For client certificates, we assume the server won't advertise
   * AUTH if they are pre-authenticated, because we also need to
   * handle the post-TLS authentication (AUTH EXTERNAL) case.
   */
  if (mutt_bit_isset (Capabilities, AUTH) &&
      (conn->account.flags & MUTT_ACCT_USER
#ifdef USE_SSL
        || SslClientCert
#endif
        ))
  {
    return smtp_auth (conn);
  }

  return 0;
}

static int smtp_auth (CONNECTION* conn)
{
  int r = SMTP_AUTH_UNAVAIL;

  if (SmtpAuthenticators)
  {
    char* methods = safe_strdup (SmtpAuthenticators);
    char* method;
    char* delim;

    for (method = methods; method; method = delim)
    {
      delim = strchr (method, ':');
      if (delim)
	*delim++ = '\0';
      if (! method[0])
	continue;

      dprint (2, (debugfile, "smtp_authenticate: Trying method %s\n", method));

      if (!ascii_strcasecmp (method, "oauthbearer"))
      {
	r = smtp_auth_oauth (conn, 0);
      }
      else if (!ascii_strcasecmp (method, "xoauth2"))
      {
	r = smtp_auth_oauth (conn, 1);
      }
      else
      {
#if defined(USE_SASL_CYRUS)
	r = smtp_auth_sasl (conn, method);
#elif defined(USE_SASL_GNU)
	r = smtp_auth_gsasl (conn, method);
#else
	mutt_error (_("SMTP authentication method %s requires SASL"), method);
	mutt_sleep (1);
	continue;
#endif
      }
      if (r == SMTP_AUTH_FAIL && delim)
      {
	mutt_error (_("%s authentication failed, trying next method"), method);
	mutt_sleep (1);
      }
      else if (r != SMTP_AUTH_UNAVAIL)
	break;
    }

    FREE (&methods);
  }
  else
  {
#if defined(USE_SASL_CYRUS)
	r = smtp_auth_sasl (conn, AuthMechs);
#elif defined(USE_SASL_GNU)
	r = smtp_auth_gsasl (conn, NULL);
#else
    mutt_error (_("SMTP authentication requires SASL"));
    mutt_sleep (1);
    r = SMTP_AUTH_UNAVAIL;
#endif
  }

  if (r != SMTP_AUTH_SUCCESS)
    mutt_account_unsetpass (&conn->account);

  if (r == SMTP_AUTH_FAIL)
  {
    mutt_error (_("SASL authentication failed"));
    mutt_sleep (1);
  }
  else if (r == SMTP_AUTH_UNAVAIL)
  {
    mutt_error (_("No authenticators available"));
    mutt_sleep (1);
  }

  return r == SMTP_AUTH_SUCCESS ? 0 : -1;
}

#ifdef USE_SASL_CYRUS
static int smtp_auth_sasl (CONNECTION* conn, const char* mechlist)
{
  sasl_conn_t* saslconn;
  sasl_interact_t* interaction = NULL;
  const char* mech;
  const char* data = NULL;
  unsigned int data_len;
  BUFFER *temp_buf = NULL, *output_buf = NULL, *smtp_response_buf = NULL;
  int rc = SMTP_AUTH_FAIL, sasl_rc, smtp_rc;

  if (mutt_sasl_client_new (conn, &saslconn) < 0)
    return SMTP_AUTH_FAIL;

  do
  {
    sasl_rc = sasl_client_start (saslconn, mechlist, &interaction, &data,
                                 &data_len, &mech);
    if (sasl_rc == SASL_INTERACT)
      mutt_sasl_interact (interaction);
  }
  while (sasl_rc == SASL_INTERACT);

  if (sasl_rc != SASL_OK && sasl_rc != SASL_CONTINUE)
  {
    dprint (2, (debugfile, "smtp_auth_sasl: %s unavailable\n", NONULL (mechlist)));
    sasl_dispose (&saslconn);
    return SMTP_AUTH_UNAVAIL;
  }

  if (!option(OPTNOCURSES))
    mutt_message (_("Authenticating (%s)..."), mech);

  temp_buf = mutt_buffer_pool_get ();
  output_buf = mutt_buffer_pool_get ();
  smtp_response_buf = mutt_buffer_pool_get ();

  mutt_buffer_printf (output_buf, "AUTH %s", mech);
  if (data_len)
  {
    mutt_buffer_addch (output_buf, ' ');
    mutt_buffer_to_base64 (temp_buf, (const unsigned char *)data, data_len);
    mutt_buffer_addstr (output_buf, mutt_b2s (temp_buf));
  }
  mutt_buffer_addstr (output_buf, "\r\n");

  do
  {
    if (mutt_socket_write (conn, mutt_b2s (output_buf)) < 0)
      goto fail;

    if (smtp_get_auth_response (conn, temp_buf, &smtp_rc, smtp_response_buf) < 0)
      goto fail;

    if (smtp_rc != smtp_ready)
      break;

    if (mutt_buffer_from_base64 (temp_buf, mutt_b2s (smtp_response_buf)) < 0)
    {
      dprint (1, (debugfile, "smtp_auth_sasl: error base64-decoding server response.\n"));
      goto fail;
    }

    do
    {
      sasl_rc = sasl_client_step (saslconn, mutt_b2s (temp_buf),
                                 mutt_buffer_len (temp_buf),
                                 &interaction, &data, &data_len);
      if (sasl_rc == SASL_INTERACT)
        mutt_sasl_interact (interaction);
    }
    while (sasl_rc == SASL_INTERACT);

    if (data_len)
      mutt_buffer_to_base64 (output_buf, (const unsigned char *)data, data_len);
    else
      mutt_buffer_clear (output_buf);
    mutt_buffer_addstr (output_buf, "\r\n");
  }
  while (sasl_rc != SASL_FAIL);

  if (smtp_success (smtp_rc))
  {
    mutt_sasl_setup_conn (conn, saslconn);
    rc = SMTP_AUTH_SUCCESS;
  }
  else
  {
    if (smtp_rc == smtp_ready)
      mutt_socket_write (conn, "*\r\n");
    sasl_dispose (&saslconn);
  }

fail:
  mutt_buffer_pool_release (&temp_buf);
  mutt_buffer_pool_release (&output_buf);
  mutt_buffer_pool_release (&smtp_response_buf);
  return rc;
}
#endif /* USE_SASL_CYRUS */

#ifdef USE_SASL_GNU
static int smtp_auth_gsasl (CONNECTION *conn, const char *method)
{
  Gsasl_session *gsasl_session = NULL;
  const char *chosen_mech;
  BUFFER *input_buf = NULL, *output_buf = NULL, *smtp_response_buf = NULL;
  char *gsasl_step_output = NULL;
  int rc = SMTP_AUTH_FAIL, gsasl_rc = GSASL_OK, smtp_rc;

  chosen_mech = mutt_gsasl_get_mech (method, AuthMechs);
  if (!chosen_mech)
  {
    dprint (2, (debugfile, "mutt_gsasl_get_mech() returned no usable mech\n"));
    return SMTP_AUTH_UNAVAIL;
  }

  dprint (2, (debugfile, "smtp_auth_gsasl: using mech %s\n", chosen_mech));

  if (mutt_gsasl_client_new (conn, chosen_mech, &gsasl_session) < 0)
  {
    dprint (1, (debugfile,
                "smtp_auth_gsasl: Error allocating GSASL connection.\n"));
    return SMTP_AUTH_UNAVAIL;
  }

  if (!option(OPTNOCURSES))
    mutt_message (_("Authenticating (%s)..."), chosen_mech);

  input_buf = mutt_buffer_pool_get ();
  output_buf = mutt_buffer_pool_get ();
  smtp_response_buf = mutt_buffer_pool_get ();

  mutt_buffer_printf (output_buf, "AUTH %s", chosen_mech);

  /* Work around broken SMTP servers. See Debian #1010658.
   * The msmtp source also forces IR for PLAIN because the author
   * encountered difficulties with a server requiring it.
   */
  if (!mutt_strcmp (chosen_mech, "PLAIN"))
  {
    gsasl_rc = gsasl_step64 (gsasl_session, "", &gsasl_step_output);
    if (gsasl_rc != GSASL_NEEDS_MORE && gsasl_rc != GSASL_OK)
    {
      dprint (1, (debugfile, "gsasl_step64() failed (%d): %s\n", gsasl_rc,
                  gsasl_strerror (gsasl_rc)));
      goto fail;
    }

    mutt_buffer_addch (output_buf, ' ');
    mutt_buffer_addstr (output_buf, gsasl_step_output);
    gsasl_free (gsasl_step_output);
  }

  mutt_buffer_addstr (output_buf, "\r\n");

  do
  {
    if (mutt_socket_write (conn, mutt_b2s (output_buf)) < 0)
      goto fail;

    if (smtp_get_auth_response (conn, input_buf, &smtp_rc, smtp_response_buf) < 0)
      goto fail;

    if (smtp_rc != smtp_ready)
      break;

    gsasl_rc = gsasl_step64 (gsasl_session, mutt_b2s (smtp_response_buf),
                             &gsasl_step_output);
    if (gsasl_rc == GSASL_NEEDS_MORE || gsasl_rc == GSASL_OK)
    {
      mutt_buffer_strcpy (output_buf, gsasl_step_output);
      mutt_buffer_addstr (output_buf, "\r\n");
      gsasl_free (gsasl_step_output);
    }
    else
    {
      dprint (1, (debugfile, "gsasl_step64() failed (%d): %s\n", gsasl_rc,
                  gsasl_strerror (gsasl_rc)));
    }
  }
  while (gsasl_rc == GSASL_NEEDS_MORE || gsasl_rc == GSASL_OK);

  if (smtp_rc == smtp_ready)
  {
    mutt_socket_write (conn, "*\r\n");
    goto fail;
  }

  if (smtp_success (smtp_rc) && (gsasl_rc == GSASL_OK))
    rc = SMTP_AUTH_SUCCESS;

fail:
  mutt_buffer_pool_release (&input_buf);
  mutt_buffer_pool_release (&output_buf);
  mutt_buffer_pool_release (&smtp_response_buf);
  mutt_gsasl_client_finish (&gsasl_session);

  if (rc == SMTP_AUTH_FAIL)
    dprint (2, (debugfile, "smtp_auth_gsasl: %s failed\n", chosen_mech));

  return rc;
}
#endif


/* smtp_auth_oauth: AUTH=OAUTHBEARER support. See RFC 7628 */
static int smtp_auth_oauth (CONNECTION* conn, int xoauth2)
{
  int rc = SMTP_AUTH_FAIL, smtp_rc;
  BUFFER *bearertoken = NULL, *authline = NULL;
  BUFFER *input_buf = NULL, *smtp_response_buf = NULL;
  const char *authtype;

  authtype = xoauth2 ? "XOAUTH2" : "OAUTHBEARER";

  /* L10N:
     %s is the authentication type, such as XOAUTH2 or OAUTHBEARER
  */
  mutt_message (_("Authenticating (%s)..."), authtype);;

  bearertoken = mutt_buffer_pool_get ();
  authline = mutt_buffer_pool_get ();
  input_buf = mutt_buffer_pool_get ();
  smtp_response_buf = mutt_buffer_pool_get ();

  /* We get the access token from the smtp_oauth_refresh_command */
  if (mutt_account_getoauthbearer (&conn->account, bearertoken, xoauth2))
    goto cleanup;

  if (xoauth2)
  {
    mutt_buffer_printf (authline, "AUTH %s\r\n", authtype);
    if (mutt_socket_write (conn, mutt_b2s (authline)) == -1)
      goto cleanup;
    if (smtp_get_auth_response (conn, input_buf, &smtp_rc, smtp_response_buf) < 0)
      goto saslcleanup;
    if (smtp_rc != smtp_ready)
      goto saslcleanup;

    mutt_buffer_printf (authline, "%s\r\n", mutt_b2s (bearertoken));
  }
  else
  {
    mutt_buffer_printf (authline, "AUTH %s %s\r\n", authtype, mutt_b2s (bearertoken));
  }

  if (mutt_socket_write (conn, mutt_b2s (authline)) == -1)
    goto cleanup;
  if (smtp_get_auth_response (conn, input_buf, &smtp_rc, smtp_response_buf) < 0)
    goto saslcleanup;
  if (!smtp_success (smtp_rc))
    goto saslcleanup;

  rc = SMTP_AUTH_SUCCESS;

saslcleanup:
  if (rc != SMTP_AUTH_SUCCESS)
  {
    /* The error response was in SASL continuation, so continue the SASL
     * to cause a failure and exit SASL input.  See RFC 7628 3.2.3
     * "AQ==" is Base64 encoded ^A (0x01) .
     */
    mutt_socket_write (conn, "AQ==\r\n");
    smtp_get_resp (conn);
  }

cleanup:
  mutt_buffer_pool_release (&bearertoken);
  mutt_buffer_pool_release (&authline);
  mutt_buffer_pool_release (&input_buf);
  mutt_buffer_pool_release (&smtp_response_buf);
  return rc;
}
