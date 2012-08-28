/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Tue Aug 28 15:48:34 2012 mstenber
 * Edit time:     17 min
 *
 */

#include "elsa_internal.h"

elsa elsa_create(elsa_client client)
{
  elsa e;
  e = elsai_calloc(client, sizeof(*e));
  e->client = client;
  if (elsai_ac_usp_get(client))
    {
      e->need_ac = true;
      e->need_originate_ac = true;
    }
  elsa_ac_init(e);
  ELSA_DEBUG("created elsa %p for client %p", e, client);
  return e;
}

void elsa_destroy(elsa e)
{
  elsa_ac_uninit(e);
  elsai_free(e->client, e);
  ELSA_DEBUG("destroyed elsa %p", e);
}

void elsa_lsa_changed(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  if (lsatype == LSA_T_AC)
    {
      ELSA_DEBUG("interesting LSA changed");
      e->need_ac = true;
    }
  else
    {
      ELSA_DEBUG("boring LSA changed (%x not %x)", lsatype, LSA_T_AC);
    }
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
      e->need_ac = false;
      elsa_ac(e);
    }
}
