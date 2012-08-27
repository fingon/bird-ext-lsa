/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Mon Aug 27 18:31:26 2012 mstenber
 * Edit time:     11 min
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
