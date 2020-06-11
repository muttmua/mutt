/*
 * Copyright (C) 1999-2001,2005 Brendan Cully <brendan@kublai.com>
 * Copyright (C) 2018 Brandon Long <blong@fiction.net>
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

/* IMAP login/authentication code */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mutt.h"
#include "imap_private.h"
#include "auth.h"

/* imap_auth_oauth: AUTH=OAUTHBEARER support. See RFC 7628 */
static imap_auth_res_t imap_auth_oauth (IMAP_DATA* idata, int xoauth2)
{
  int rc = IMAP_AUTH_FAILURE, steprc;
  BUFFER *bearertoken = NULL, *authline = NULL;
  const char *authtype;

  authtype = xoauth2 ? "XOAUTH2" : "OAUTHBEARER";

  mutt_message (_("Authenticating (%s)..."), authtype);

  bearertoken = mutt_buffer_pool_get ();
  authline = mutt_buffer_pool_get ();

  /* We get the access token from the imap_oauth_refresh_command */
  if (mutt_account_getoauthbearer (&idata->conn->account, bearertoken, xoauth2))
    goto cleanup;

  mutt_buffer_printf (authline, "AUTHENTICATE %s %s",
                      authtype, mutt_b2s (bearertoken));

  /* This doesn't really contain a password, but the token is good for
   * an hour, so suppress it anyways.
   */
  if (imap_exec (idata, mutt_b2s (authline), IMAP_CMD_FAIL_OK | IMAP_CMD_PASS))
  {
    /* The error response was in SASL continuation, so continue the SASL
     * to cause a failure and exit SASL input.  See RFC 7628 3.2.3
     * "AQ==" is Base64 encoded ^A (0x01) .
     */
    mutt_socket_write (idata->conn, "AQ==\r\n");
    do
      steprc = imap_cmd_step (idata);
    while (steprc == IMAP_CMD_CONTINUE);

    /* L10N:
       %s is the authentication type, for example OAUTHBEARER
    */
    mutt_error (_("%s authentication failed."), authtype);
    mutt_sleep (2);
    goto cleanup;
  }

  mutt_clear_error();
  rc = IMAP_AUTH_SUCCESS;

cleanup:
  mutt_buffer_pool_release (&bearertoken);
  mutt_buffer_pool_release (&authline);
  return rc;
}

/* AUTH=OAUTHBEARER support. See RFC 7628 */
imap_auth_res_t imap_auth_oauthbearer (IMAP_DATA* idata, const char* method)
{
  /* For now, we only support SASL_IR also and over TLS */
  if (!mutt_bit_isset (idata->capabilities, SASL_IR) ||
      !idata->conn->ssf)
    return IMAP_AUTH_UNAVAIL;

  if (!mutt_bit_isset (idata->capabilities, AUTH_OAUTHBEARER))
    return IMAP_AUTH_UNAVAIL;

  if (!ImapOauthRefreshCmd)
    return IMAP_AUTH_UNAVAIL;

  return imap_auth_oauth (idata, 0);
}

/* AUTH=XOAUTH2 support. */
imap_auth_res_t imap_auth_xoauth2 (IMAP_DATA* idata, const char* method)
{
  /* For now, we only support SASL_IR also and over TLS */
  if (!mutt_bit_isset (idata->capabilities, SASL_IR) ||
      !idata->conn->ssf)
    return IMAP_AUTH_UNAVAIL;

  if (!mutt_bit_isset (idata->capabilities, AUTH_XOAUTH2))
    return IMAP_AUTH_UNAVAIL;

  /* If they did not explicitly request XOAUTH2 then fail quietly */
  if (!(method && ImapOauthRefreshCmd))
      return IMAP_AUTH_UNAVAIL;

  return imap_auth_oauth (idata, 1);
}
