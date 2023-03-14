/*
 * Copyright (C) 2021-2022 Kevin J. McCarthy <kevin@8t8.us>
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
#include "account.h"
#include "mutt_sasl_gnu.h"
#include "mutt_socket.h"

#include <errno.h>
#include <gsasl.h>

static Gsasl *mutt_gsasl_ctx = NULL;

static int mutt_gsasl_callback (Gsasl *ctx, Gsasl_session *sctx,
                                Gsasl_property prop);


/* mutt_gsasl_start: called before doing a SASL exchange - initialises library
 *   (if necessary). */
static int mutt_gsasl_init (void)
{
  int rc;

  if (mutt_gsasl_ctx)
    return 0;

  rc = gsasl_init (&mutt_gsasl_ctx);
  if (rc != GSASL_OK)
  {
    mutt_gsasl_ctx = NULL;
    dprint (1, (debugfile,
                "mutt_gsasl_start: libgsasl initialisation failed (%d): %s.\n",
                rc, gsasl_strerror (rc)));
    return -1;
  }

  gsasl_callback_set (mutt_gsasl_ctx, mutt_gsasl_callback);

  return 0;
}

void mutt_gsasl_done (void)
{
  if (mutt_gsasl_ctx)
  {
    gsasl_done (mutt_gsasl_ctx);
    mutt_gsasl_ctx = NULL;
  }
}

static const char *VALID_MECHANISM_CHARACTERS =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";

/* This logic is derived from the libgsasl suggest code */
static int mechlist_contains (const char *uc_mech, const char *uc_mechlist)
{
  size_t mech_len, mechlist_len, fragment_len, i;

  mech_len = mutt_strlen (uc_mech);
  mechlist_len = mutt_strlen (uc_mechlist);

  if (!mech_len || !mechlist_len)
    return 0;

  for (i = 0; i < mechlist_len;)
  {
    fragment_len = strspn (uc_mechlist + i, VALID_MECHANISM_CHARACTERS);
    if ((mech_len == fragment_len) &&
        !ascii_strncmp (uc_mech, uc_mechlist + i, mech_len))
      return 1;
    i += fragment_len + 1;
  }
  return 0;
}

const char *mutt_gsasl_get_mech (const char *requested_mech,
                                 const char *server_mechlist)
{
  char *uc_requested_mech, *uc_server_mechlist;
  const char *rv = NULL;

  if (mutt_gsasl_init ())
    return NULL;

  /* libgsasl does not do case-independent string comparisons, and stores
   * its methods internally in uppercase.
   */
  uc_server_mechlist = safe_strdup (server_mechlist);
  if (uc_server_mechlist)
    ascii_strupper (uc_server_mechlist);

  uc_requested_mech = safe_strdup (requested_mech);
  if (uc_requested_mech)
    ascii_strupper (uc_requested_mech);

  if (uc_requested_mech)
  {
    if (mechlist_contains (uc_requested_mech, uc_server_mechlist))
      rv = gsasl_client_suggest_mechanism (mutt_gsasl_ctx, uc_requested_mech);
  }
  else
    rv = gsasl_client_suggest_mechanism (mutt_gsasl_ctx, uc_server_mechlist);

  FREE (&uc_requested_mech);
  FREE (&uc_server_mechlist);

  return rv;
}

int mutt_gsasl_client_new (CONNECTION *conn, const char *mech,
                           Gsasl_session **sctx)
{
  int rc;

  if (mutt_gsasl_init ())
    return -1;

  rc = gsasl_client_start (mutt_gsasl_ctx, mech, sctx);
  if (rc != GSASL_OK)
  {
    *sctx = NULL;
    dprint (1, (debugfile,
                "mutt_gsasl_client_new: gsasl_client_start failed (%d): %s.\n",
                rc, gsasl_strerror (rc)));
    return -1;
  }

  gsasl_session_hook_set (*sctx, conn);

  return 0;
}

void mutt_gsasl_client_finish (Gsasl_session **sctx)
{
  gsasl_finish (*sctx);
  *sctx = NULL;
}


static int mutt_gsasl_callback (Gsasl *ctx, Gsasl_session *sctx,
                                Gsasl_property prop)
{
  int rc = GSASL_NO_CALLBACK;
  CONNECTION *conn;
  const char* service;

  conn = gsasl_session_hook_get (sctx);
  if (!conn)
  {
    dprint (1, (debugfile, "mutt_gsasl_callback(): missing session hook data!\n"));
    return rc;
  }

  switch (prop)
  {
    case GSASL_PASSWORD:
      if (mutt_account_getpass (&conn->account))
        return rc;
      gsasl_property_set (sctx, GSASL_PASSWORD, conn->account.pass);
      rc = GSASL_OK;
      break;

    case GSASL_AUTHID:
      /* whom the provided password belongs to: login */
      if (mutt_account_getlogin (&conn->account))
        return rc;
      gsasl_property_set (sctx, GSASL_AUTHID, conn->account.login);
      rc = GSASL_OK;
      break;

    case GSASL_AUTHZID:
      /* name of the user whose mail/resources you intend to access: user */
      if (mutt_account_getuser (&conn->account))
        return rc;
      gsasl_property_set (sctx, GSASL_AUTHZID, conn->account.user);
      rc = GSASL_OK;
      break;

    case GSASL_ANONYMOUS_TOKEN:
      gsasl_property_set (sctx, GSASL_ANONYMOUS_TOKEN, "dummy");
      rc = GSASL_OK;
      break;

    case GSASL_SERVICE:
      switch (conn->account.type)
      {
        case MUTT_ACCT_TYPE_IMAP:
          service = "imap";
          break;
        case MUTT_ACCT_TYPE_POP:
          service = "pop";
          break;
        case MUTT_ACCT_TYPE_SMTP:
          service = "smtp";
          break;
        default:
          return rc;
      }
      gsasl_property_set (sctx, GSASL_SERVICE, service);
      rc = GSASL_OK;
      break;

    case GSASL_HOSTNAME:
      gsasl_property_set (sctx, GSASL_HOSTNAME, conn->account.host);
      rc = GSASL_OK;
      break;

    default:
      break;
  }

  return rc;
}
