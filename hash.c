/*
 * Copyright (C) 1996-2009 Michael R. Elkins <me@mutt.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "mutt.h"

#define SOMEPRIME 149711

static unsigned int gen_string_hash(union hash_key key, unsigned int n)
{
  unsigned int h = 0;
  const unsigned char *s = (const unsigned char *)key.strkey;

  while (*s)
    h += (h << 7) + *s++;
  h = (h * SOMEPRIME) % n;

  return h;
}

static int cmp_string_key(union hash_key a, union hash_key b)
{
  return mutt_strcmp(a.strkey, b.strkey);
}

static unsigned int gen_case_string_hash(union hash_key key, unsigned int n)
{
  unsigned int h = 0;
  const unsigned char *s = (const unsigned char *)key.strkey;

  while (*s)
    h += (h << 7) + tolower(*s++);
  h = (h * SOMEPRIME) % n;

  return h;
}

static int cmp_case_string_key(union hash_key a, union hash_key b)
{
  return mutt_strcasecmp(a.strkey, b.strkey);
}

static unsigned int gen_int_hash(union hash_key key, unsigned int n)
{
  return key.intkey % n;
}

static int cmp_int_key(union hash_key a, union hash_key b)
{
  if (a.intkey == b.intkey)
    return 0;
  if (a.intkey < b.intkey)
    return -1;
  return 1;
}

static HASH *new_hash(int bucket_count)
{
  HASH *table = safe_calloc(1, sizeof(HASH));
  if (bucket_count == 0)
    bucket_count = 2;
  table->bucket_count = bucket_count;
  table->table = safe_calloc(bucket_count, sizeof(struct hash_elem *));
  return table;
}

HASH *hash_create(int bucket_count, int flags)
{
  HASH *table = new_hash(bucket_count);
  if (flags & MUTT_HASH_STRCASECMP)
  {
    table->gen_hash = gen_case_string_hash;
    table->cmp_key = cmp_case_string_key;
  }
  else
  {
    table->gen_hash = gen_string_hash;
    table->cmp_key = cmp_string_key;
  }
  if (flags & MUTT_HASH_STRDUP_KEYS)
    table->strdup_keys = 1;
  if (flags & MUTT_HASH_ALLOW_DUPS)
    table->allow_dups = 1;
  return table;
}

HASH *int_hash_create(int bucket_count, int flags)
{
  HASH *table = new_hash(bucket_count);
  table->gen_hash = gen_int_hash;
  table->cmp_key = cmp_int_key;
  if (flags & MUTT_HASH_ALLOW_DUPS)
    table->allow_dups = 1;
  return table;
}

static void resize_hash(HASH *table)
{
  struct hash_elem **old_buckets = table->table;
  int old_bucket_count = table->bucket_count;
  int bucket, hash;
  struct hash_elem *elem, *next_elem;

  while (table->elem_count > table->bucket_count * 1.2)
    table->bucket_count *= 2;
  table->table = safe_calloc(table->bucket_count, sizeof(struct hash_elem *));

  for (bucket = 0; bucket < old_bucket_count; bucket++)
  {
    elem = old_buckets[bucket];
    while (elem)
    {
      next_elem = elem->next;

      hash = table->gen_hash(elem->key, table->bucket_count);
      elem->next = table->table[hash];
      table->table[hash] = elem;

      elem = next_elem;
    }
  }
  FREE(&old_buckets);
}

/* table        hash table to update
 * key          key to hash on
 * data         data to associate with `key'
 * allow_dup    if nonzero, duplicate keys are allowed in the table
 */
static int union_hash_insert(HASH *table, union hash_key key, void *data)
{
  struct hash_elem *ptr;
  unsigned int h;

  if (table->elem_count > table->bucket_count * 1.2)
    resize_hash(table);

  ptr = (struct hash_elem *) safe_malloc(sizeof(struct hash_elem));
  h = table->gen_hash(key, table->bucket_count);
  ptr->key = key;
  ptr->data = data;

  if (table->allow_dups)
  {
    ptr->next = table->table[h];
    table->table[h] = ptr;
    table->elem_count++;
  }
  else
  {
    struct hash_elem *tmp, *last;
    int r;

    for (tmp = table->table[h], last = NULL; tmp; last = tmp, tmp = tmp->next)
    {
      r = table->cmp_key(tmp->key, key);
      if (r == 0)
      {
        FREE(&ptr);
        return (-1);
      }
      if (r > 0)
        break;
    }
    if (last)
      last->next = ptr;
    else
      table->table[h] = ptr;
    ptr->next = tmp;
    table->elem_count++;
  }
  return h;
}

int hash_insert(HASH *table, const char *strkey, void *data)
{
  union hash_key key;
  key.strkey = table->strdup_keys ? safe_strdup(strkey) : strkey;
  return union_hash_insert(table, key, data);
}

int int_hash_insert(HASH *table, unsigned int intkey, void *data)
{
  union hash_key key;
  key.intkey = intkey;
  return union_hash_insert(table, key, data);
}

static struct hash_elem *union_hash_find_elem(const HASH *table, union hash_key key)
{
  int hash;
  struct hash_elem *ptr;

  if (!table)
    return NULL;

  hash = table->gen_hash(key, table->bucket_count);
  ptr = table->table[hash];
  for (; ptr; ptr = ptr->next)
  {
    if (table->cmp_key(key, ptr->key) == 0)
      return (ptr);
  }
  return NULL;
}

static void *union_hash_find(const HASH *table, union hash_key key)
{
  struct hash_elem *ptr = union_hash_find_elem(table, key);
  if (ptr)
    return ptr->data;
  else
    return NULL;
}

void *hash_find(const HASH *table, const char *strkey)
{
  union hash_key key;
  key.strkey = strkey;
  return union_hash_find(table, key);
}

struct hash_elem *hash_find_elem(const HASH *table, const char *strkey)
{
  union hash_key key;
  key.strkey = strkey;
  return union_hash_find_elem(table, key);
}

void *int_hash_find(const HASH *table, unsigned int intkey)
{
  union hash_key key;
  key.intkey = intkey;
  return union_hash_find(table, key);
}

struct hash_elem *hash_find_bucket(const HASH *table, const char *strkey)
{
  union hash_key key;
  int hash;

  if (!table)
    return NULL;

  key.strkey = strkey;
  hash = table->gen_hash(key, table->bucket_count);
  return table->table[hash];
}

static void union_hash_delete(HASH *table, union hash_key key, const void *data,
                              void (*destroy)(void *))
{
  int hash;
  struct hash_elem *ptr, **last;

  if (!table)
    return;

  hash = table->gen_hash(key, table->bucket_count);
  ptr = table->table[hash];
  last = &table->table[hash];

  while (ptr)
  {
    if ((data == ptr->data || !data)
        && table->cmp_key(ptr->key, key) == 0)
    {
      *last = ptr->next;
      if (destroy)
        destroy(ptr->data);
      if (table->strdup_keys)
        FREE(&ptr->key.strkey);
      FREE(&ptr);
      table->elem_count--;

      ptr = *last;
    }
    else
    {
      last = &ptr->next;
      ptr = ptr->next;
    }
  }
}

void hash_delete(HASH *table, const char *strkey, const void *data,
                 void (*destroy)(void *))
{
  union hash_key key;
  key.strkey = strkey;
  union_hash_delete(table, key, data, destroy);
}

void int_hash_delete(HASH *table, unsigned int intkey, const void *data,
                     void (*destroy)(void *))
{
  union hash_key key;
  key.intkey = intkey;
  union_hash_delete(table, key, data, destroy);
}

/* ptr          pointer to the hash table to be freed
 * destroy()    function to call to free the ->data member (optional)
 */
void hash_destroy(HASH **ptr, void (*destroy)(void *))
{
  int i;
  HASH *pptr;
  struct hash_elem *elem, *tmp;

  if (!ptr || !*ptr)
    return;

  pptr = *ptr;
  for (i = 0 ; i < pptr->bucket_count; i++)
  {
    for (elem = pptr->table[i]; elem; )
    {
      tmp = elem;
      elem = elem->next;
      if (destroy)
        destroy(tmp->data);
      if (pptr->strdup_keys)
        FREE(&tmp->key.strkey);
      FREE(&tmp);
    }
  }
  FREE(&pptr->table);
  FREE(ptr);           /* __FREE_CHECKED__ */
}

struct hash_elem *hash_walk(const HASH *table, struct hash_walk_state *state)
{
  if (state->last && state->last->next)
  {
    state->last = state->last->next;
    return state->last;
  }

  if (state->last)
    state->index++;

  while (state->index < table->bucket_count)
  {
    if (table->table[state->index])
    {
      state->last = table->table[state->index];
      return state->last;
    }
    state->index++;
  }

  state->index = 0;
  state->last = NULL;
  return NULL;
}
