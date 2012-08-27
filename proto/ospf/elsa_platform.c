/*
 * $Id: elsa_platform.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:14:38 2012 mstenber
 * Last modified: Mon Aug 27 16:03:14 2012 mstenber
 * Edit time:     36 min
 *
 */

#include "ospf.h"
#include "elsa.h"
#include "elsa_internal.h"

#include <assert.h>

/********************************************************** Memory handling  */

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

/*************************************************************** General API */

uint32_t elsai_get_rid(elsa_client client)
{
  return client->router_id;
}

/************************************************************** LSA handling */

uint32_t elsai_lsa_get_rid(elsa_lsa lsa)
{
  struct top_hash_entry *en = lsa->hash_entry;

  assert(en);
  return htonl(en->lsa.rt);
}

uint32_t elsai_lsa_get_lsid(elsa_lsa lsa)
{
  struct top_hash_entry *en = lsa->hash_entry;

  assert(en);
  return htonl(en->lsa.id);
}

void elsai_lsa_get_body(elsa_lsa lsa, unsigned char **body, size_t *body_len)
{
  struct top_hash_entry *en = lsa->hash_entry;
  int len;

  assert(en);
  len = en->lsa.length - sizeof(struct ospf_lsa_header);
  htonlsab(en->lsa_body, lsa->dummy_lsa_buf, len);
  *body = (void *)lsa->dummy_lsa_buf;
  *body_len = len;
}

static elsa_lsa find_next_entry(elsa_client client, elsa_lsa lsa)
{
  int i;
  struct top_graph *gr = client->gr;
  struct top_hash_entry *e;
  for (i = lsa->hash_bin ; i < gr->hash_size ; i++)
    if ((e=gr->hash_table[i]))
      {
        lsa->hash_entry = e;
        lsa->hash_bin = i;
        return lsa;
      }
  return NULL;
}

elsa_lsa elsai_get_lsa_by_type(elsa_client client, elsa_lsatype lsatype)
{
  int idx =
    client->elsa->platform.last_lsa++ % SUPPORTED_SIMULTANEOUS_LSA_ITERATIONS;
  elsa_lsa lsa =
    &client->elsa->platform.lsa[idx];
  lsa->hash_bin = 0;
  lsa->hash_entry = NULL;
  return find_next_entry(client, lsa);
}

elsa_lsa elsai_get_lsa_by_type_next(elsa_client client, elsa_lsa lsa)
{
  assert(lsa->hash_entry);
  if (lsa->hash_entry->next)
    {
      lsa->hash_entry = lsa->hash_entry->next;
      return lsa;
    }
  return find_next_entry(client, lsa);
}

/*************************************************************** IF handling */

const char *elsai_if_get_name(elsa_client client, elsa_if i)
{
  if (!i->iface)
    return NULL;

  return i->iface->name;
}


uint32_t elsai_if_get_index(elsa_client client, elsa_if i)
{
  if (!i->iface)
    return 0;

  return htonl(i->iface->index);
}

uint32_t elsai_if_get_neigh_iface_id(elsa_client client,
                                     elsa_if i,
                                     uint32_t rid)
{
  struct ospf_neighbor *neigh;

  WALK_LIST(neigh, i->neigh_list)
    {
      if (neigh->state >= NEIGHBOR_INIT)
        {
          if (neigh->rid == ntohl(rid))
            {
              return htonl(neigh->iface_id);
            }
        }
    }
  return 0;
}

uint8_t elsai_if_get_priority(elsa_client client, elsa_if i)
{
  /* XXX - someday put interface config back in? */
  return 50;
}

elsa_if elsai_if_get(elsa_client client)
{
  /* Should be just 1 area, but hell.. :-) */
  return HEAD(client->iface_list);
}

elsa_if elsai_if_get_next(elsa_client client, elsa_if ifp)
{
  return NODE_NEXT(ifp);
}


void elsai_lsa_originate(elsa_client client,
                         elsa_lsatype lsatype,
                         uint32_t lsid,
                         uint32_t sn,
                         void *body, size_t body_len)
{
  struct ospf_lsa_header lsa;
  bool need_ac_save;

  lsa.age = 0;
  lsa.type = ntohs(lsatype);
  lsa.id = ntohl(lsid);
  lsa.rt = client->router_id;
  lsa.sn = ntohl(sn);
  u32 dom = 0;
  lsa.length = body_len + sizeof(struct ospf_lsa_header);
  lsasum_calculate(&lsa, body);

  need_ac_save = client->elsa->need_ac;
  client->elsa->need_ac = false;
  (void)lsa_install_new(client, &lsa, dom, body);

  /* If the AC really _did_ change, ELSA got the notification and set
   * it's need_ac.. As this may have led to USP available changing, we
   * may need to run the AC algorithm again. Regardless, we flood
   * first what we have. */
  if (client->elsa->need_ac)
    ospf_lsupd_flood(client, NULL, NULL, &lsa, dom, 1);
  else
    client->elsa->need_ac = need_ac_save;
}

/*************************************************** Configured USP handling */

elsa_ac_usp elsai_ac_usp_get(elsa_client client)
{
  /* XXX */
  return NULL;
}

/* Get next available usable prefix */
elsa_ac_usp elsai_ac_usp_get_next(elsa_client client, elsa_ac_usp usp)
{
  /* XXX */
  return NULL;
}

/* Get the prefix's contents. The result_size is the size of result in bits,
 * and result pointer itself points at the prefix data. */
void elsai_ac_usp_get_prefix(elsa_client client, elsa_ac_usp usp,
                             void **result, int *result_size_bits)
{
  *result = NULL;
  *result_size_bits = 0;
}
