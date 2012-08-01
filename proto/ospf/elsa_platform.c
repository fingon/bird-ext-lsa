/*
 * $Id: elsa_platform.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:14:38 2012 mstenber
 * Last modified: Wed Aug  1 14:18:09 2012 mstenber
 * Edit time:     2 min
 *
 */

#include "ospf.h"
#include "elsa.h"

void *elsai_calloc(elsa_client client, size_t size)
{
  struct proto *p = &client->proto;
  void *t = mb_allocz(p->pool, size);
  return t;
}

void elsai_free(elsa_client client, void *ptr)
{
  mb_free(ptr);
}
