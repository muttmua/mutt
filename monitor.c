/* * Copyright (C) 2018 Gero Treuner <gero@70t.de>
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

#if HAVE_SYS_INOTIFY_H
# include <sys/types.h>
# include <sys/inotify.h>
# include <unistd.h>
# include <poll.h>
#endif
#ifndef HAVE_INOTIFY_INIT1
# include <fcntl.h>
#endif

#include "mutt.h"
#include "buffy.h"
#include "monitor.h"
#include "mx.h"
#include "mutt_curses.h"

#include <errno.h>
#include <sys/stat.h>

typedef struct monitor_t
{
  struct monitor_t *next;
  char *mh_backup_path;
  dev_t st_dev;
  ino_t st_ino;
  short magic;
  int descr;
}
MONITOR;

static int INotifyFd = -1;
static MONITOR *Monitor = NULL;
static size_t PollFdsCount = 0;
static size_t PollFdsLen = 0;
static struct pollfd *PollFds;

static int MonitorContextDescriptor = -1;

typedef struct monitorinfo_t
{
  short magic;
  short isdir;
  const char *path;
  dev_t st_dev;
  ino_t st_ino;
  MONITOR *monitor;
  BUFFER *_pathbuf; /* access via path only (maybe not initialized) */
}
MONITORINFO;

#define INOTIFY_MASK_DIR  (IN_MOVED_TO | IN_ATTRIB | IN_CLOSE_WRITE | IN_ISDIR)
#define INOTIFY_MASK_FILE IN_CLOSE_WRITE

static void mutt_poll_fd_add(int fd, short events)
{
  int i = 0;
  for (i = 0; i < PollFdsCount && PollFds[i].fd != fd; ++i);

  if (i == PollFdsCount)
  {
    if (PollFdsCount == PollFdsLen)
    {
      PollFdsLen += 2;
      safe_realloc (&PollFds, PollFdsLen * sizeof(struct pollfd));
    }
    ++PollFdsCount;
    PollFds[i].fd = fd;
    PollFds[i].events = events;
  }
  else
    PollFds[i].events |= events;
}

static int mutt_poll_fd_remove(int fd)
{
  int i = 0, d;
  for (i = 0; i < PollFdsCount && PollFds[i].fd != fd; ++i);
  if (i == PollFdsCount)
    return -1;
  d = PollFdsCount - i - 1;
  if (d)
    memmove (&PollFds[i], &PollFds[i + 1], d * sizeof(struct pollfd));
  --PollFdsCount;
  return 0;
}

static int monitor_init (void)
{
  if (INotifyFd == -1)
  {
#if HAVE_INOTIFY_INIT1
    INotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (INotifyFd == -1)
    {
      dprint (2, (debugfile, "monitor: inotify_init1 failed, errno=%d %s\n", errno, strerror(errno)));
      return -1;
    }
#else
    INotifyFd = inotify_init();
    if (INotifyFd == -1)
    {
      dprint (2, (debugfile, "monitor: inotify_init failed, errno=%d %s\n", errno, strerror(errno)));
      return -1;
    }
    fcntl(INotifyFd, F_SETFL, O_NONBLOCK);
    fcntl(INotifyFd, F_SETFD, FD_CLOEXEC);
#endif
    mutt_poll_fd_add(0, POLLIN);
    mutt_poll_fd_add(INotifyFd, POLLIN);
  }
  return 0;
}

static void monitor_check_free (void)
{
  if (!Monitor && INotifyFd != -1)
  {
    mutt_poll_fd_remove(INotifyFd);
    close (INotifyFd);
    INotifyFd = -1;
    MonitorFilesChanged = 0;
  }
}

static MONITOR *monitor_create (MONITORINFO *info, int descriptor)
{
  MONITOR *monitor = (MONITOR *) safe_calloc (1, sizeof (MONITOR));
  monitor->magic  = info->magic;
  monitor->st_dev = info->st_dev;
  monitor->st_ino = info->st_ino;
  monitor->descr  = descriptor;
  monitor->next   = Monitor;
  if (info->magic == MUTT_MH)
    monitor->mh_backup_path = safe_strdup(info->path);

  Monitor = monitor;

  return monitor;
}

static void monitor_info_init (MONITORINFO *info)
{
  memset (info, 0, sizeof (MONITORINFO));
}

static void monitor_info_free (MONITORINFO *info)
{
  mutt_buffer_free (&info->_pathbuf);
}

static void monitor_delete (MONITOR *monitor)
{
  MONITOR **ptr = &Monitor;

  if (!monitor)
    return;

  FOREVER
  {
    if (!*ptr)
      return;
    if (*ptr == monitor)
      break;
    ptr = &(*ptr)->next;
  }

  FREE (&monitor->mh_backup_path); /* __FREE_CHECKED__ */
  monitor = monitor->next;
  FREE (ptr); /* __FREE_CHECKED__ */
  *ptr = monitor;
}

static int monitor_handle_ignore (int descr)
{
  int new_descr = -1;
  MONITOR *iter = Monitor;
  struct stat sb;

  while (iter && iter->descr != descr)
    iter = iter->next;

  if (iter)
  {
    if (iter->magic == MUTT_MH && stat (iter->mh_backup_path, &sb) == 0)
    {
      if ((new_descr = inotify_add_watch (INotifyFd, iter->mh_backup_path, INOTIFY_MASK_FILE)) == -1)
        dprint (2, (debugfile, "monitor: inotify_add_watch failed for '%s', errno=%d %s\n", iter->mh_backup_path, errno, strerror(errno)));
      else
      {
        dprint (3, (debugfile, "monitor: inotify_add_watch descriptor=%d for '%s'\n", descr, iter->mh_backup_path));
        iter->st_dev = sb.st_dev;
        iter->st_ino = sb.st_ino;
        iter->descr = new_descr;
      }
    }
    else
    {
      dprint (3, (debugfile, "monitor: cleanup watch (implicitly removed) - descriptor=%d\n", descr));
    }

    if (MonitorContextDescriptor == descr)
      MonitorContextDescriptor = new_descr;

    if (new_descr == -1)
    {
      monitor_delete (iter);
      monitor_check_free ();
    }
  }

  return new_descr;
}

#define EVENT_BUFLEN MAX(4096, sizeof(struct inotify_event) + NAME_MAX + 1)

/* mutt_monitor_poll: Waits for I/O ready file descriptors or signals.
 *
 * return values:
 *      -3   unknown/unexpected events: poll timeout / fds not handled by us
 *      -2   monitor detected changes, no STDIN input
 *      -1   error (see errno)
 *       0   (1) input ready from STDIN, or (2) monitoring inactive -> no poll()
 * MonitorFilesChanged also reflects changes to monitored files.
 *
 * Only STDIN and INotify file handles currently expected/supported.
 * More would ask for common infrastructur (sockets?).
 */
int mutt_monitor_poll (void)
{
  int rc = 0, fds, i, inputReady;
  char buf[EVENT_BUFLEN]
    __attribute__ ((aligned(__alignof__(struct inotify_event))));

  MonitorFilesChanged = 0;

  if (INotifyFd != -1)
  {
    fds = poll (PollFds, PollFdsCount, MuttGetchTimeout);

    if (fds == -1)
    {
      rc = -1;
      if (errno != EINTR)
      {
        dprint (2, (debugfile, "monitor: poll() failed, errno=%d %s\n", errno, strerror(errno)));
      }
    }
    else
    {
      inputReady = 0;
      for (i = 0; fds && i < PollFdsCount; ++i)
      {
        if (PollFds[i].revents)
        {
          --fds;
          if (PollFds[i].fd == 0)
          {
            inputReady = 1;
          }
          else if (PollFds[i].fd == INotifyFd)
          {
            MonitorFilesChanged = 1;
            dprint (3, (debugfile, "monitor: file change(s) detected\n"));
            int len;
            char *ptr = buf;
            const struct inotify_event *event;

            FOREVER
            {
              len = read (INotifyFd, buf, sizeof(buf));
              if (len == -1)
              {
                if (errno != EAGAIN)
                  dprint (2, (debugfile, "monitor: read inotify events failed, errno=%d %s\n",
                              errno, strerror(errno)));
                break;
              }

              while (ptr < buf + len)
              {
                event = (const struct inotify_event *) ptr;
                dprint (5, (debugfile, "monitor:  + detail: descriptor=%d mask=0x%x\n",
                            event->wd, event->mask));
                if (event->mask & IN_IGNORED)
                  monitor_handle_ignore (event->wd);
                else if (event->wd == MonitorContextDescriptor)
                  MonitorContextChanged = 1;
                ptr += sizeof(struct inotify_event) + event->len;
              }
            }
          }
        }
      }
      if (!inputReady)
        rc = MonitorFilesChanged ? -2 : -3;
    }
  }

  return rc;
}

#define RESOLVERES_OK_NOTEXISTING  0
#define RESOLVERES_OK_EXISTING     1
#define RESOLVERES_FAIL_NOMAILBOX -3
#define RESOLVERES_FAIL_NOMAGIC   -2
#define RESOLVERES_FAIL_STAT      -1

/* monitor_resolve: resolve monitor entry match by BUFFY, or - if NULL - by Context.
 *
 * return values:
 *      >=0   mailbox is valid and locally accessible:
 *              0: no monitor / 1: preexisting monitor
 *       -3   no mailbox (MONITORINFO: no fields set)
 *       -2   magic not set
 *       -1   stat() failed (see errno; MONITORINFO fields: magic, isdir, path)
 */
static int monitor_resolve (MONITORINFO *info, BUFFY *buffy)
{
  MONITOR *iter;
  char *fmt = NULL;
  struct stat sb;

  if (buffy)
  {
    info->magic = buffy->magic;
    info->path  = buffy->realpath;
  }
  else if (Context)
  {
    info->magic = Context->magic;
    info->path  = Context->realpath;
  }
  else
  {
    return RESOLVERES_FAIL_NOMAILBOX;
  }

  if (!info->magic)
  {
    return RESOLVERES_FAIL_NOMAGIC;
  }
  else if (info->magic == MUTT_MAILDIR)
  {
    info->isdir = 1;
    fmt = "%s/new";
  }
  else
  {
    info->isdir = 0;
    if (info->magic == MUTT_MH)
      fmt = "%s/.mh_sequences";
  }

  if (fmt)
  {
    if (!info->_pathbuf)
      info->_pathbuf = mutt_buffer_new ();
    mutt_buffer_printf (info->_pathbuf, fmt, info->path);
    info->path = mutt_b2s (info->_pathbuf);
  }
  if (stat (info->path, &sb) != 0)
    return RESOLVERES_FAIL_STAT;

  iter = Monitor;
  while (iter && (iter->st_ino != sb.st_ino || iter->st_dev != sb.st_dev))
    iter = iter->next;

  info->st_dev = sb.st_dev;
  info->st_ino = sb.st_ino;
  info->monitor = iter;

  return iter ? RESOLVERES_OK_EXISTING : RESOLVERES_OK_NOTEXISTING;
}

/* mutt_monitor_add: add file monitor from BUFFY, or - if NULL - from Context.
 *
 * return values:
 *       0   success: new or already existing monitor
 *      -1   failed:  no mailbox, inaccessible file, create monitor/watcher failed
 */
int mutt_monitor_add (BUFFY *buffy)
{
  MONITORINFO info;
  uint32_t mask;
  int descr, rc = 0;

  monitor_info_init (&info);

  descr = monitor_resolve (&info, buffy);
  if (descr != RESOLVERES_OK_NOTEXISTING)
  {
    if (!buffy && (descr == RESOLVERES_OK_EXISTING))
      MonitorContextDescriptor = info.monitor->descr;
    rc = descr == RESOLVERES_OK_EXISTING ? 0 : -1;
    goto cleanup;
  }

  mask = info.isdir ? INOTIFY_MASK_DIR : INOTIFY_MASK_FILE;
  if ((INotifyFd == -1 && monitor_init () == -1)
      || (descr = inotify_add_watch (INotifyFd, info.path, mask)) == -1)
  {
    dprint (2, (debugfile, "monitor: inotify_add_watch failed for '%s', errno=%d %s\n", info.path, errno, strerror(errno)));
    rc = -1;
    goto cleanup;
  }

  dprint (3, (debugfile, "monitor: inotify_add_watch descriptor=%d for '%s'\n", descr, info.path));
  if (!buffy)
    MonitorContextDescriptor = descr;

  monitor_create (&info, descr);

cleanup:
  monitor_info_free (&info);
  return rc;
}

/* mutt_monitor_remove: remove file monitor from BUFFY, or - if NULL - from Context.
 *
 * return values:
 *       0   monitor removed (not shared)
 *       1   monitor not removed (shared)
 *       2   no monitor
 */
int mutt_monitor_remove (BUFFY *buffy)
{
  MONITORINFO info, info2;
  int rc = 0;

  monitor_info_init (&info);
  monitor_info_init (&info2);

  if (!buffy)
  {
    MonitorContextDescriptor = -1;
    MonitorContextChanged = 0;
  }

  if (monitor_resolve (&info, buffy) != RESOLVERES_OK_EXISTING)
  {
    rc = 2;
    goto cleanup;
  }

  if (Context)
  {
    if (buffy)
    {
      if (monitor_resolve (&info2, NULL) == RESOLVERES_OK_EXISTING
          && info.st_ino == info2.st_ino && info.st_dev == info2.st_dev)
      {
        rc = 1;
        goto cleanup;
      }
    }
    else
    {
      if (mutt_find_mailbox (Context->realpath))
      {
        rc = 1;
        goto cleanup;
      }
    }
  }

  inotify_rm_watch(info.monitor->descr, INotifyFd);
  dprint (3, (debugfile, "monitor: inotify_rm_watch for '%s' descriptor=%d\n", info.path, info.monitor->descr));

  monitor_delete (info.monitor);
  monitor_check_free ();

cleanup:
  monitor_info_free (&info);
  monitor_info_free (&info2);
  return rc;
}
