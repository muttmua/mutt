/*
 * Copyright (C) 2021 Kevin J. McCarthy <kevin@8t8.us>
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

/* NOTE: This file is split out because term.h pollutes the namespace,
 *       e.g. columns.  Better to have the problem be in one tiny
 *       file than everywhere that includes mutt_curses.h.
 */

#if !defined(USE_SLANG_CURSES) && defined(HAVE_TERM_H)
#include <term.h>

const char *mutt_tigetstr (const char *capname)
{
  return tigetstr (capname);
}

int mutt_tigetflag (const char *capname)
{
  return tigetflag (capname);
}

#else
const char *mutt_tigetstr (const char *capname)
{
  return NULL;
}

int mutt_tigetflag (const char *capname)
{
  return 0;
}
#endif
