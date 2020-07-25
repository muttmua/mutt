/*
 * Copyright (C) 2000-2001 Vsevolod Volkov <vvv@mutt.org.ua>
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
#include "mx.h"
#include "md5.h"
#include "pop.h"

#include <string.h>
#include <unistd.h>

#ifdef USE_SASL
#include <sasl/sasl.h>
#include <sasl/saslutil.h>

#include "mutt_sasl.h"
#endif

#ifdef USE_SASL
/* SASL authenticator */
static pop_auth_res_t pop_auth_sasl (POP_DATA *pop_data, const char *method)
{
  sasl_conn_t *saslconn;
  sasl_interact_t *interaction = NULL;
  int rc;
  char *buf = NULL;
  size_t bufsize = 0;
  char inbuf[LONG_STRING];
  const char* mech;
  const char *pc = NULL;
  unsigned int len, olen, client_start;

  if (mutt_account_getpass (&pop_data->conn->account) ||
      !pop_data->conn->account.pass[0])
    return POP_A_FAILURE;

  if (mutt_sasl_client_new (pop_data->conn, &saslconn) < 0)
  {
    dprint (1, (debugfile, "pop_auth_sasl: Error allocating SASL connection.\n"));
    return POP_A_FAILURE;
  }

  if (!method)
    method = pop_data->auth_list;

  FOREVER
  {
    rc = sasl_client_start(saslconn, method, &interaction, &pc, &olen, &mech);
    if (rc != SASL_INTERACT)
      break;
    mutt_sasl_interact (interaction);
  }

  if (rc != SASL_OK && rc != SASL_CONTINUE)
  {
    dprint (1, (debugfile, "pop_auth_sasl: Failure starting authentication exchange. No shared mechanisms?\n"));

    /* SASL doesn't support suggested mechanisms, so fall back */
    sasl_dispose (&saslconn);
    return POP_A_UNAVAIL;
  }

  /* About client_start:  If sasl_client_start() returns data via pc/olen,
   * the client is expected to send this first (after the AUTH string is sent).
   * sasl_client_start() may in fact return SASL_OK in this case.
   */
  client_start = olen;

  mutt_message _("Authenticating (SASL)...");

  bufsize = ((olen * 2) > LONG_STRING) ? (olen * 2) : LONG_STRING;
  buf = safe_malloc (bufsize);

  snprintf (buf, bufsize, "AUTH %s", mech);
  olen = strlen (buf);

  /* looping protocol */
  FOREVER
  {
    strfcpy (buf + olen, "\r\n", bufsize - olen);
    mutt_socket_write (pop_data->conn, buf);
    if (mutt_socket_readln (inbuf, sizeof (inbuf), pop_data->conn) < 0)
    {
      sasl_dispose (&saslconn);
      pop_data->status = POP_DISCONNECTED;
      FREE (&buf);
      return POP_A_SOCKET;
    }

    /* Note we don't exit if rc==SASL_OK when client_start is true.
     * This is because the first loop has only sent the AUTH string, we
     * need to loop at least once more to send the pc/olen returned
     * by sasl_client_start().
     */
    if (!client_start && rc != SASL_CONTINUE)
      break;

    if (!mutt_strncmp (inbuf, "+ ", 2)
        && sasl_decode64 (inbuf+2, strlen (inbuf+2), buf, bufsize - 1, &len) != SASL_OK)
    {
      dprint (1, (debugfile, "pop_auth_sasl: error base64-decoding server response.\n"));
      goto bail;
    }

    if (!client_start)
      FOREVER
      {
	rc = sasl_client_step (saslconn, buf, len, &interaction, &pc, &olen);
	if (rc != SASL_INTERACT)
	  break;
	mutt_sasl_interact (interaction);
      }
    else
    {
      olen = client_start;
      client_start = 0;
    }

    /* Even if sasl_client_step() returns SASL_OK, we should send at
     * least one more line to the server.  See #3862.
     */
    if (rc != SASL_CONTINUE && rc != SASL_OK)
      break;

    /* send out response, or line break if none needed */
    if (pc)
    {
      if ((olen * 2) > bufsize)
      {
        bufsize = olen * 2;
        safe_realloc (&buf, bufsize);
      }
      if (sasl_encode64 (pc, olen, buf, bufsize, &olen) != SASL_OK)
      {
	dprint (1, (debugfile, "pop_auth_sasl: error base64-encoding client response.\n"));
	goto bail;
      }
    }
  }

  if (rc != SASL_OK)
    goto bail;

  if (!mutt_strncmp (inbuf, "+OK", 3))
  {
    mutt_sasl_setup_conn (pop_data->conn, saslconn);
    FREE (&buf);
    return POP_A_SUCCESS;
  }

bail:
  sasl_dispose (&saslconn);

  /* terminate SASL session if the last response is not +OK nor -ERR */
  if (!mutt_strncmp (inbuf, "+ ", 2))
  {
    snprintf (buf, bufsize, "*\r\n");
    if (pop_query (pop_data, buf, sizeof (buf)) == -1)
    {
      FREE (&buf);
      return POP_A_SOCKET;
    }
  }

  FREE (&buf);
  mutt_error _("SASL authentication failed.");
  mutt_sleep (2);

  return POP_A_FAILURE;
}
#endif

/* Get the server timestamp for APOP authentication */
void pop_apop_timestamp (POP_DATA *pop_data, char *buf)
{
  char *p1, *p2;

  FREE (&pop_data->timestamp);

  if ((p1 = strchr (buf, '<')) && (p2 = strchr (p1, '>')))
  {
    p2[1] = '\0';
    pop_data->timestamp = safe_strdup (p1);
  }
}

/* APOP authenticator */
static pop_auth_res_t pop_auth_apop (POP_DATA *pop_data, const char *method)
{
  struct md5_ctx ctx;
  unsigned char digest[16];
  char hash[33];
  char buf[LONG_STRING];
  size_t i;

  if (mutt_account_getpass (&pop_data->conn->account) ||
      !pop_data->conn->account.pass[0])
    return POP_A_FAILURE;

  if (!pop_data->timestamp)
    return POP_A_UNAVAIL;

  if (rfc822_valid_msgid (pop_data->timestamp) < 0)
  {
    mutt_error _("POP timestamp is invalid!");
    mutt_sleep (2);
    return POP_A_UNAVAIL;
  }

  mutt_message _("Authenticating (APOP)...");

  /* Compute the authentication hash to send to the server */
  md5_init_ctx (&ctx);
  md5_process_bytes (pop_data->timestamp, strlen (pop_data->timestamp), &ctx);
  md5_process_bytes (pop_data->conn->account.pass,
		     strlen (pop_data->conn->account.pass), &ctx);
  md5_finish_ctx (&ctx, digest);

  for (i = 0; i < sizeof (digest); i++)
    sprintf (hash + 2 * i, "%02x", digest[i]);

  /* Send APOP command to server */
  snprintf (buf, sizeof (buf), "APOP %s %s\r\n", pop_data->conn->account.user, hash);

  switch (pop_query (pop_data, buf, sizeof (buf)))
  {
    case 0:
      return POP_A_SUCCESS;
    case -1:
      return POP_A_SOCKET;
  }

  mutt_error _("APOP authentication failed.");
  mutt_sleep (2);

  return POP_A_FAILURE;
}

/* USER authenticator */
static pop_auth_res_t pop_auth_user (POP_DATA *pop_data, const char *method)
{
  char buf[LONG_STRING];
  int ret;

  if (!pop_data->cmd_user)
    return POP_A_UNAVAIL;

  if (mutt_account_getpass (&pop_data->conn->account) ||
      !pop_data->conn->account.pass[0])
    return POP_A_FAILURE;

  mutt_message _("Logging in...");

  snprintf (buf, sizeof (buf), "USER %s\r\n", pop_data->conn->account.user);
  ret = pop_query (pop_data, buf, sizeof (buf));

  if (pop_data->cmd_user == 2)
  {
    if (ret == 0)
    {
      pop_data->cmd_user = 1;

      dprint (1, (debugfile, "pop_auth_user: set USER capability\n"));
    }

    if (ret == -2)
    {
      pop_data->cmd_user = 0;

      dprint (1, (debugfile, "pop_auth_user: unset USER capability\n"));
      snprintf (pop_data->err_msg, sizeof (pop_data->err_msg), "%s",
                _("Command USER is not supported by server."));
    }
  }

  if (ret == 0)
  {
    snprintf (buf, sizeof (buf), "PASS %s\r\n", pop_data->conn->account.pass);
    ret = pop_query_d (pop_data, buf, sizeof (buf),
#ifdef DEBUG
                       /* don't print the password unless we're at the ungodly debugging level */
                       debuglevel < MUTT_SOCK_LOG_FULL ? "PASS *\r\n" :
#endif
                       NULL);
  }

  switch (ret)
  {
    case 0:
      return POP_A_SUCCESS;
    case -1:
      return POP_A_SOCKET;
  }

  mutt_error ("%s %s", _("Login failed."), pop_data->err_msg);
  mutt_sleep (2);

  return POP_A_FAILURE;
}

/* OAUTHBEARER/XOAUTH2 authenticator */
static pop_auth_res_t pop_auth_oauth (POP_DATA *pop_data, int xoauth2)
{
  int rc = POP_A_FAILURE;
  BUFFER *bearertoken = NULL, *authline = NULL;
  const char *authtype;
  char decoded_err[LONG_STRING];
  char *err = NULL;
  int ret, len;

  authtype = xoauth2 ? "XOAUTH2" : "OAUTHBEARER";

  mutt_message (_("Authenticating (%s)..."), authtype);

  bearertoken = mutt_buffer_pool_get ();
  authline = mutt_buffer_pool_get ();

  if (mutt_account_getoauthbearer (&pop_data->conn->account, bearertoken, xoauth2))
    goto cleanup;

  mutt_buffer_printf (authline, "AUTH %s\r\n", authtype);
  ret = pop_query (pop_data, authline->data, authline->dsize);

  if (ret == 0 ||
      (ret == -2 && !mutt_strncmp (authline->data, "+", 1)))
  {
    mutt_buffer_printf (authline, "%s\r\n", mutt_b2s (bearertoken));

    ret = pop_query_d (pop_data, authline->data, authline->dsize,
#ifdef DEBUG
                       /* don't print the bearer token unless we're at the ungodly debugging level */
                       debuglevel < MUTT_SOCK_LOG_FULL ?
                       (xoauth2 ? "*\r\n" : "*\r\n")
                       :
#endif
                       NULL);
  }

  switch (ret)
  {
    case 0:
      rc = POP_A_SUCCESS;
      goto cleanup;
    case -1:
      rc = POP_A_SOCKET;
      goto cleanup;
  }

  /* The error response was a SASL continuation, so "continue" it.
   * See RFC 7628 3.2.3.  "AQ==" is Base64 encoded ^A (0x01) .
   */
  err = pop_data->err_msg;
  len = mutt_from_base64 (decoded_err, pop_data->err_msg, sizeof(decoded_err) - 1);
  if (len >= 0)
  {
    decoded_err[len] = '\0';
    err = decoded_err;
  }
  mutt_buffer_strcpy (authline, "AQ==\r\n");
  pop_query_d (pop_data, authline->data, authline->dsize, NULL);
  mutt_error ("%s %s", _("Authentication failed."), err);
  mutt_sleep (2);

cleanup:
  mutt_buffer_pool_release (&bearertoken);
  mutt_buffer_pool_release (&authline);
  return rc;
}


/* OAUTHBEARER/XOAUTH2 authenticator */
static pop_auth_res_t pop_auth_oauthbearer (POP_DATA *pop_data, const char *method)
{
  if (!PopOauthRefreshCmd)
    return POP_A_UNAVAIL;

  return pop_auth_oauth (pop_data, 0);
}

/* OAUTHBEARER/XOAUTH2 authenticator */
static pop_auth_res_t pop_auth_xoauth2 (POP_DATA *pop_data, const char *method)
{
  /* If they did not explicitly request XOAUTH2 then fail quietly */
  if (!(method && PopOauthRefreshCmd))
    return POP_A_UNAVAIL;

  return pop_auth_oauth (pop_data, 1);
}


static const pop_auth_t pop_authenticators[] = {
  { pop_auth_oauthbearer, "oauthbearer" },
  { pop_auth_xoauth2, "xoauth2" },
#ifdef USE_SASL
  { pop_auth_sasl, NULL },
#endif
  { pop_auth_apop, "apop" },
  { pop_auth_user, "user" },
  { NULL,	   NULL }
};

/*
 * Authentication
 *  0 - successful,
 * -1 - connection lost,
 * -2 - login failed,
 * -3 - authentication canceled.
 */
int pop_authenticate (POP_DATA* pop_data)
{
  ACCOUNT *acct = &pop_data->conn->account;
  const pop_auth_t* authenticator;
  char* methods;
  char* comma;
  char* method;
  int attempts = 0;
  int ret = POP_A_UNAVAIL;

  if (mutt_account_getuser (acct) || !acct->user[0])
    return -3;

  if (PopAuthenticators)
  {
    /* Try user-specified list of authentication methods */
    methods = safe_strdup (PopAuthenticators);
    method = methods;

    while (method)
    {
      comma = strchr (method, ':');
      if (comma)
	*comma++ = '\0';
      dprint (2, (debugfile, "pop_authenticate: Trying method %s\n", method));
      authenticator = pop_authenticators;

      while (authenticator->authenticate)
      {
	if (!authenticator->method ||
	    !ascii_strcasecmp (authenticator->method, method))
	{
	  ret = authenticator->authenticate (pop_data, method);
	  if (ret == POP_A_SOCKET)
	    switch (pop_connect (pop_data))
	    {
	      case 0:
	      {
		ret = authenticator->authenticate (pop_data, method);
		break;
	      }
	      case -2:
		ret = POP_A_FAILURE;
	    }

	  if (ret != POP_A_UNAVAIL)
	    attempts++;
	  if (ret == POP_A_SUCCESS || ret == POP_A_SOCKET ||
	      (ret == POP_A_FAILURE && !option (OPTPOPAUTHTRYALL)))
	  {
	    comma = NULL;
	    break;
	  }
	}
	authenticator++;
      }

      method = comma;
    }

    FREE (&methods);
  }
  else
  {
    /* Fall back to default: any authenticator */
    dprint (2, (debugfile, "pop_authenticate: Using any available method.\n"));
    authenticator = pop_authenticators;

    while (authenticator->authenticate)
    {
      ret = authenticator->authenticate (pop_data, NULL);
      if (ret == POP_A_SOCKET)
	switch (pop_connect (pop_data))
	{
	  case 0:
	  {
	    ret = authenticator->authenticate (pop_data, NULL);
	    break;
	  }
	  case -2:
	    ret = POP_A_FAILURE;
	}

      if (ret != POP_A_UNAVAIL)
	attempts++;
      if (ret == POP_A_SUCCESS || ret == POP_A_SOCKET ||
	  (ret == POP_A_FAILURE && !option (OPTPOPAUTHTRYALL)))
	break;

      authenticator++;
    }
  }

  switch (ret)
  {
    case POP_A_SUCCESS:
      return 0;
    case POP_A_SOCKET:
      return -1;
    case POP_A_UNAVAIL:
      if (!attempts)
	mutt_error (_("No authenticators available"));
  }

  return -2;
}
