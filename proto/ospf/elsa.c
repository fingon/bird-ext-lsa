/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Wed Aug  1 14:28:14 2012 mstenber
 * Edit time:     6 min
 *
 */

#include "elsa_internal.h"

elsa elsa_create(elsa_client client)
{
  elsa e;
  e = elsai_calloc(client, sizeof(*e));
  e->client = client;
  elsa_ac_init(e);
  return e;
}

void elsa_destroy(elsa e)
{
  elsa_ac_uninit(e);
  elsai_free(e->client, e);
}

void elsa_lsa_changed(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  if (lsatype == LSA_T_AC)
    e->need_ac = true;
}

void elsa_lsa_deleted(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  e->need_ac = true;
}

bool elsa_supports_lsatype(elsa_lsatype lsatype)
{
  return lsatype == LSA_T_AC;
}

void elsa_dispatch(elsa e)
{
  if (e->need_ac)
    {
      elsa_ac(e);
      e->need_ac = false;
    }
}
