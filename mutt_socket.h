/*
 * Copyright (C) 1998 Brandon Long <blong@fiction.net>
 * Copyright (C) 1999-2005 Brendan Cully <brendan@kublai.com>
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

#ifndef _MUTT_SOCKET_H_
#define _MUTT_SOCKET_H_ 1

#include "account.h"
#include "lib.h"

/* logging levels */
#define MUTT_SOCK_LOG_CMD  2
#define MUTT_SOCK_LOG_HDR  3
#define MUTT_SOCK_LOG_FULL 4

typedef struct _connection
{
  ACCOUNT account;
  /* security strength factor, in bits (in theory).
   *
   * In actuality, Mutt uses this as a boolean to determine if the connection
   * is "secure" using TLS or $tunnel if $tunnel_is_secure is set.
   *
   * The value is passed to SASL, but since no min_ssf is also passed to SASL
   * I don't believe it makes any difference.
   *
   * The GnuTLS code currently even puts bytes in here, so I doubt the exact
   * value has significance for Mutt purposes.
   */
  unsigned int ssf;
  void *data;

  char inbuf[LONG_STRING];
  int bufpos;

  int fd;
  int available;

  struct _connection *next;

  void *sockdata;
  int (*conn_read) (struct _connection* conn, char* buf, size_t len);
  int (*conn_write) (struct _connection *conn, const char *buf, size_t count);
  int (*conn_open) (struct _connection *conn);
  int (*conn_close) (struct _connection *conn);
  int (*conn_poll) (struct _connection *conn, time_t wait_secs);
} CONNECTION;

int mutt_socket_open (CONNECTION* conn);
int mutt_socket_close (CONNECTION* conn);
int mutt_socket_has_buffered_input (CONNECTION *conn);
void mutt_socket_clear_buffered_input (CONNECTION *conn);
int mutt_socket_poll (CONNECTION* conn, time_t wait_secs);
int mutt_socket_readchar (CONNECTION *conn, char *c);
#define mutt_socket_buffer_readln(A,B) mutt_socket_buffer_readln_d(A,B,MUTT_SOCK_LOG_CMD)
int mutt_socket_buffer_readln_d (BUFFER *buf, CONNECTION *conn, int dbg);
#define mutt_socket_readln(A,B,C) mutt_socket_readln_d(A,B,C,MUTT_SOCK_LOG_CMD)
int mutt_socket_readln_d (char *buf, size_t buflen, CONNECTION *conn, int dbg);
#define mutt_socket_write(A,B) mutt_socket_write_d(A,B,-1,MUTT_SOCK_LOG_CMD)
#define mutt_socket_write_n(A,B,C) mutt_socket_write_d(A,B,C,MUTT_SOCK_LOG_CMD)
int mutt_socket_write_d (CONNECTION *conn, const char *buf, int len, int dbg);

/* stupid hack for imap_logout_all */
CONNECTION* mutt_socket_head (void);
void mutt_socket_free (CONNECTION* conn);
CONNECTION* mutt_conn_find (const CONNECTION* start, const ACCOUNT* account);

int raw_socket_read (CONNECTION* conn, char* buf, size_t len);
int raw_socket_write (CONNECTION* conn, const char* buf, size_t count);
int raw_socket_open (CONNECTION *conn);
int raw_socket_close (CONNECTION *conn);
int raw_socket_poll (CONNECTION* conn, time_t wait_secs);

#endif /* _MUTT_SOCKET_H_ */
