/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@mutt.org>
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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#else
# ifdef HAVE_IOCTL_H
# include <ioctl.h>
# endif
#endif

/* this routine should be called after receiving SIGWINCH */
void mutt_resize_screen(void)
{
#ifndef USE_SLANG_CURSES
  char *cp;
  int fd;
  struct winsize w;
  int screen_rows, screen_cols;

  screen_rows = -1;
  screen_cols = -1;
  if ((fd = open("/dev/tty", O_RDONLY)) != -1)
  {
    if (ioctl(fd, TIOCGWINSZ, &w) != -1)
    {
      screen_rows = w.ws_row;
      screen_cols = w.ws_col;
    }
    close(fd);
  }
  if (screen_rows <= 0)
  {
    if ((cp = getenv("LINES")) != NULL)
      mutt_atoi(cp, &screen_rows, 0);
    if (screen_rows <= 0)
      screen_rows = 24;
  }
  if (screen_cols <= 0)
  {
    if ((cp = getenv("COLUMNS")) != NULL)
      mutt_atoi(cp, &screen_cols, 0);
    if (screen_cols <= 0)
      screen_cols = 80;
  }

  resizeterm(screen_rows, screen_cols);
#else
  SLtt_get_screen_size();
  SLsmg_reinit_smg();
  delwin(stdscr);
  stdscr = newwin(0, 0, 0, 0);
  keypad(stdscr, TRUE);
#endif

  mutt_reflow_windows();
}
