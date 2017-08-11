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

/* common protos for compose / attach menus */

#ifndef _ATTACH_H_
#define _ATTACH_H_ 1

#include "mutt_menu.h"

typedef struct attachptr
{
  BODY *content;
  int parent_type;
  char *tree;
  int level;
  int num;
  unsigned int unowned : 1;   /* don't unlink on detach */
} ATTACHPTR;

typedef struct attach_ctx
{
  ATTACHPTR **idx;
  short idxlen;
  short idxmax;
} ATTACH_CONTEXT;

void mutt_gen_attach_list (ATTACH_CONTEXT *, BODY *, int, int, int);
void mutt_update_tree (ATTACH_CONTEXT *);
int mutt_view_attachment (FILE*, BODY *, int, HEADER *, ATTACH_CONTEXT *);

int mutt_tag_attach (MUTTMENU *menu, int n, int m);
int mutt_attach_display_loop (MUTTMENU *menu, int op, FILE *fp, HEADER *hdr,
			      BODY *cur, ATTACH_CONTEXT *acvtx,
			      int recv);


void mutt_save_attachment_list (FILE *fp, int tag, BODY *top, HEADER *hdr, MUTTMENU *menu);
void mutt_pipe_attachment_list (FILE *fp, int tag, BODY *top, int filter);
void mutt_print_attachment_list (FILE *fp, int tag, BODY *top);

void mutt_attach_bounce (FILE *, HEADER *, ATTACH_CONTEXT *, BODY *);
void mutt_attach_resend (FILE *, HEADER *, ATTACH_CONTEXT *, BODY *);
void mutt_attach_forward (FILE *, HEADER *, ATTACH_CONTEXT *, BODY *);
void mutt_attach_reply (FILE *, HEADER *, ATTACH_CONTEXT *, BODY *, int);

void mutt_actx_add_attach (ATTACH_CONTEXT *actx, ATTACHPTR *attach, MUTTMENU *menu);
void mutt_actx_free_entries (ATTACH_CONTEXT *actx);
void mutt_free_attach_context (ATTACH_CONTEXT **pactx);

#endif /* _ATTACH_H_ */
