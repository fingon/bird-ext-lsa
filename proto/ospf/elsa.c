/*
 * $Id: elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:01:30 2012 mstenber
 * Last modified: Wed Aug  1 14:04:43 2012 mstenber
 * Edit time:     3 min
 *
 */

#include "elsa.h"

/* function code 8176(0x1FF0): experimental, U-bit=1, Area Scope */
#define LSA_T_AC        0xBFF0 /* Auto-Configuration LSA */

struct elsa_struct {
  bool need_prefix_assignment;
};

elsa elsa_create(elsa_client client)
{
  elsa e;
  e = elsai_calloc(sizeof(*e));
  return e;
}

void elsa_destroy(elsa e)
{
  elsai_free(e);
}

void elsa_lsa_changed(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  e->need_prefix_assignment = true;
}

void elsa_lsa_deleted(elsa e, elsa_lsatype lsatype)
{
  assert(e);
  e->need_prefix_assignment = true;
}

bool elsa_supports_lsatype(elsa_lsatype lsatype)
{
  return lsatype == LSA_T_AC;
}

void elsa_dispatch(elsa e)
{
  if (need_prefix_assignment)
    {
      /* XXX */
    }
}
