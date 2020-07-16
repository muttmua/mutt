/*
 * Copyright (C) 2020 Kevin J. McCarthy <kevin@8t8.us>
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
#include "mutt_lisp.h"

#include <ctype.h>
#include <stdlib.h>

static int read_backticks (BUFFER *dest, BUFFER *line)
{
  char ch;
  int rc = -1, matched = 0;

  if (*line->dptr != '`')
    return -1;

  mutt_buffer_addch (dest, *line->dptr++);

  while (*line->dptr && !matched)
  {
    ch = *line->dptr;

    if (ch == '`')
      matched = 1;
    else if (ch == '\\')
    {
      mutt_buffer_addch (dest, *line->dptr++);
      if (!*line->dptr)
        break;
    }

    mutt_buffer_addch (dest, *line->dptr++);
  }

  if (matched)
    rc = 0;
  else
  {
    /* L10N:
       Printed when backticks in MuttLisp are not matched.
    */
    mutt_error (_("MuttLisp: unclosed backticks: %s"), mutt_b2s (line));
  }

  return rc;
}

static int read_list (BUFFER *list, BUFFER *line)
{
  int rc = -1, level = 0;
  char ch, quotechar = 0;

  mutt_buffer_clear (list);
  if (!line->dptr || !*line->dptr)
    return 0;

  SKIPWS (line->dptr);

  if (*line->dptr != '(')
    return -1;

  mutt_buffer_addch (list, *line->dptr++);
  level = 1;

  while (*line->dptr && level)
  {
    ch = *line->dptr;

    if (ch == quotechar)
      quotechar = 0;
    else if (ch == '\\' && quotechar != '\'')
    {
      mutt_buffer_addch (list, *line->dptr++);
      if (!*line->dptr)
        break;
    }
    else if (ch == '`' && (!quotechar || quotechar == '"'))
    {
      read_backticks (list, line);
      continue;
    }
    else if (!quotechar)
    {
      if (ch == '"' || ch == '\'')
        quotechar = ch;
      else if (ch == '(')
        level++;
      else if (ch == ')')
        level--;
    }

    mutt_buffer_addch (list, *line->dptr++);
  }

  if (level == 0)
    rc = 0;
  else
  {
    /* L10N:
       Printed when a MuttLisp list is missing a matching closing
       parenthesis.  For example: (a
    */
    mutt_error (_("MuttLisp: unclosed list: %s"), mutt_b2s (line));
  }

  return rc;
}

static int read_atom (BUFFER *atom, BUFFER *line)
{
  char ch, quotechar = 0;
  int rc = 0;

  mutt_buffer_clear (atom);
  if (!line->dptr || !*line->dptr)
    return 0;

  SKIPWS (line->dptr);

  while (*line->dptr)
  {
    ch = *line->dptr;

    if (ch == quotechar)
      quotechar = 0;
    else if (ch == '\\' && quotechar != '\'')
    {
      mutt_buffer_addch (atom, *line->dptr++);
      if (!*line->dptr)
        break;
    }
    else if (ch == '`' && (!quotechar || quotechar == '"'))
    {
      rc = read_backticks (atom, line);
      continue;
    }
    else if (!quotechar)
    {
      if (ISSPACE (ch) || ch == '(' || ch == ')')
        break;
      if (ch == '"' || ch == '\'')
        quotechar = ch;
    }

    mutt_buffer_addch (atom, *line->dptr++);
  }

  return rc;
}

/* returns -1 on an illegal sexp
 *         0 end of line (denoted by an empty sexp)
 *         1 valid sexp read in
 */
static int read_sexp (BUFFER *sexp, BUFFER *line)
{
  int rc = 0;

  mutt_buffer_clear (sexp);
  if (!line->dptr || !*line->dptr)
    return 0;

  SKIPWS (line->dptr);

  if (*line->dptr == '(')
    rc = read_list (sexp, line);
  else if (*line->dptr)
    rc = read_atom (sexp, line);

  if (rc == 0 && mutt_buffer_len (sexp))
    rc = 1;

  return rc;
}

/* note that a valid sexp might have been read in, but expand to an empty one.
 *
 * returns -1 on an illegal sexp
 *         0 end of line (denoted by an empty sexp)
 *         1 valid sexp read in
 */
static int read_eval_sexp (BUFFER *sexp, BUFFER *line)
{
  int rc = -1;
  BUFFER *temp_sexp = NULL;

  mutt_buffer_clear (sexp);
  if (!line->dptr || !*line->dptr)
    return 0;

  temp_sexp = mutt_buffer_new ();
  mutt_buffer_increase_size (temp_sexp, mutt_buffer_len (line));

  rc = read_sexp (temp_sexp, line);
  if (rc <= 0)
    goto cleanup;

  temp_sexp->dptr = temp_sexp->data;

  /* We send evaluation back through the muttrc parser.
   * Variables/expressions will be handled there, while list evaluation
   * will be done by calling mutt_lisp_eval_list().
   *
   * If we ever decide to add types (e.g. lists or variables), we
   * could instead directly handle their evaluation internally.  The
   * external mutt_lisp_eval_list() would always return the string
   * form, while the internal version could handle more.
   */
  if (mutt_extract_token (sexp, temp_sexp,
                          MUTT_TOKEN_LISP | MUTT_TOKEN_COMMENT |
                          MUTT_TOKEN_SEMICOLON) != 0)
  {
    rc = -1;
    goto cleanup;
  }

  /* It's possible mutt_extract_token() did not process the entire
   * temp_sexp.  For instance backticks might have generated multiple
   * words of output that weren't all processed. Prepend that to line
   * so it is processed later.
   */
  if (*temp_sexp->dptr)
  {
    BUFFER *extra = mutt_buffer_pool_get ();
    mutt_buffer_strcpy (extra, temp_sexp->dptr);
    mutt_buffer_addstr (extra, line->dptr);
    mutt_buffer_strcpy (line, mutt_b2s (extra));
    line->dptr = line->data;
    mutt_buffer_pool_release (&extra);
  }

  rc = 1;

cleanup:
  mutt_buffer_free (&temp_sexp);
  return rc;
}

static int lisp_concat (BUFFER *result, BUFFER *list)
{
  BUFFER *arg = NULL;
  int rc;

  mutt_buffer_clear (result);

  arg = mutt_buffer_new ();

  rc = read_eval_sexp (arg, list);
  while (rc > 0)
  {
    mutt_buffer_addstr (result, mutt_b2s (arg));
    rc = read_eval_sexp (arg, list);
  }

  mutt_buffer_free (&arg);

  return rc;
}

static int lisp_quote (BUFFER *result, BUFFER *list)
{
  mutt_buffer_strcpy (result, list->dptr);
  return 0;
}

static int lisp_equal (BUFFER *result, BUFFER *list)
{
  BUFFER *first_arg = NULL, *arg = NULL;
  int rc;

  mutt_buffer_clear (result);
  mutt_buffer_addch (result, 't');

  first_arg = mutt_buffer_new ();
  rc = read_eval_sexp (first_arg, list);
  if (rc <= 0)
    goto cleanup;

  arg = mutt_buffer_new ();
  rc = read_eval_sexp (arg, list);
  while (rc > 0)
  {
    if (mutt_strcmp (mutt_b2s (first_arg), mutt_b2s (arg)))
    {
      mutt_buffer_clear (result);
      break;
    }
    rc = read_eval_sexp (arg, list);
  }

cleanup:
  mutt_buffer_free (&first_arg);
  mutt_buffer_free (&arg);

  return rc;
}

static int lisp_not (BUFFER *result, BUFFER *list)
{
  BUFFER *arg = NULL;
  int rc;

  mutt_buffer_clear (result);

  arg = mutt_buffer_new ();

  rc = read_eval_sexp (arg, list);
  if (rc > 0 && !mutt_buffer_len (arg))
    mutt_buffer_addstr (result, "t");

  mutt_buffer_free (&arg);

  return rc;
}

static int lisp_and (BUFFER *result, BUFFER *list)
{
  BUFFER *arg = NULL;
  int rc;

  mutt_buffer_clear (result);
  mutt_buffer_addch (result, 't');

  arg = mutt_buffer_new ();

  rc = read_eval_sexp (arg, list);
  while (rc > 0)
  {
    mutt_buffer_strcpy (result, mutt_b2s (arg));
    if (!mutt_buffer_len (result))
      break;
    rc = read_eval_sexp (arg, list);
  }

  mutt_buffer_free (&arg);

  return rc;
}

static int lisp_or (BUFFER *result, BUFFER *list)
{
  BUFFER *arg = NULL;
  int rc;

  mutt_buffer_clear (result);

  arg = mutt_buffer_new ();

  rc = read_eval_sexp (arg, list);
  while (rc > 0)
  {
    mutt_buffer_strcpy (result, mutt_b2s (arg));
    if (mutt_buffer_len (result))
      break;
    rc = read_eval_sexp (arg, list);
  }

  mutt_buffer_free (&arg);

  return rc;
}

static int lisp_if (BUFFER *result, BUFFER *list)
{
  BUFFER *cond = NULL;
  int rc;

  mutt_buffer_clear (result);

  cond = mutt_buffer_new ();

  rc = read_eval_sexp (cond, list);
  if (rc <= 0)
  {
    /* L10N:
       An error printed for the 'if' function if the condition is missing.
       For example (if) by itself.
    */
    mutt_error (_("MuttLisp: missing if condition: %s"), mutt_b2s (list));
    goto cleanup;
  }

  if (!mutt_buffer_len (cond))
    read_sexp (result, list);
  rc = read_eval_sexp (result, list);

cleanup:
  mutt_buffer_free (&cond);

  return rc;
}

typedef struct
{
  char *name;
  int (*func) (BUFFER *, BUFFER *);
} lisp_func_t;

const lisp_func_t LispFunctions[] = {
  {"concat",       lisp_concat},
  {"quote",        lisp_quote},
  {"equal",        lisp_equal},
  {"not",          lisp_not},
  {"and",          lisp_and},
  {"or",           lisp_or},
  {"if",           lisp_if},
  {NULL,           NULL}
};

static int eval_function (BUFFER *result, const char *func, BUFFER *list)
{
  int i, rc = -1;

  for (i = 0; LispFunctions[i].name; i++)
  {
    if (!mutt_strcmp (func, LispFunctions[i].name))
    {
      rc = LispFunctions[i].func (result, list);
      break;
    }
  }
  if (!LispFunctions[i].name)
  {
    /* L10N:
       Printed when a function is called that is not recognized by MuttLisp.
    */
    mutt_error (_("MuttLisp: no such function %s"), NONULL (func));
  }

  return rc;
}

int mutt_lisp_eval_list (BUFFER *result, BUFFER *line)
{
  int rc = -1;
  BUFFER *list = NULL, *function = NULL;

  mutt_buffer_clear (result);
  if (!line->dptr || !*line->dptr)
    return 0;

  list = mutt_buffer_new ();
  mutt_buffer_increase_size (list, mutt_buffer_len (line));

  if (read_list (list, line) != 0)
    goto cleanup;

  /* Rewind list and trim the outer parens */
  *(list->dptr - 1) = '\0';
  list->dptr = list->data + 1;

  function = mutt_buffer_new ();
  if (read_sexp (function, list) <= 0)
    goto cleanup;
  SKIPWS (list->dptr);

  if (eval_function (result, mutt_b2s (function), list) < 0)
    goto cleanup;

  rc = 0;

cleanup:
  mutt_buffer_free (&list);
  mutt_buffer_free (&function);
  return rc;
}
