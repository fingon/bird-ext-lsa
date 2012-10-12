/*
 * $Id: elsa_platform.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:14:38 2012 mstenber
 * Last modified: Fri Oct 12 13:17:13 2012 mstenber
 * Edit time:     79 min
 *
 */

#include "ospf.h"
#include "elsa.h"
#include "elsa_internal.h"

#include <assert.h>

#include "lib/md5.h"

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

void ospf_dridd_trigger(struct proto_ospf *po);

void elsai_change_rid(elsa_client client)
{
  ospf_dridd_trigger(client);
}

/************************************************************** LSA handling */

uint32_t elsai_lsa_get_rid(elsa_lsa lsa)
{
  struct top_hash_entry *en = lsa->hash_entry;

  assert(en);
  return en->lsa.rt;
}


elsa_lsatype elsai_lsa_get_type(elsa_lsa lsa)
{
  struct top_hash_entry *en = lsa->hash_entry;

  assert(en);
  return en->lsa.type;
}

uint32_t elsai_lsa_get_lsid(elsa_lsa lsa)
{
  struct top_hash_entry *en = lsa->hash_entry;

  assert(en);
  return en->lsa.id;
}

void elsai_lsa_get_body(elsa_lsa lsa, unsigned char **body, size_t *body_len)
{
  struct top_hash_entry *en = lsa->hash_entry;
  int len;

  assert(en);
  len = en->lsa.length - sizeof(struct ospf_lsa_header);
  if (lsa->swapped)
    {
      htonlsab(en->lsa_body, lsa->dummy_lsa_buf, len);
      *body = (void *)lsa->dummy_lsa_buf;
    }
  else
    {
      *body = en->lsa_body;
    }
  *body_len = len;
}

static elsa_lsa find_next_entry(elsa_client client,
                                elsa_lsa lsa,
                                elsa_lsatype type)
{
  int i;
  struct top_graph *gr = client->gr;
  struct top_hash_entry *e;
  for (i = lsa->hash_bin ; i < gr->hash_size ; i++)
    if ((e=gr->hash_table[i]))
      {
        while (e)
          {
            if (e->lsa.type != type)
              {
                e = e->next;
                continue;
              }
            lsa->hash_entry = e;
            lsa->swapped = true;
            lsa->hash_bin = i;
            return lsa;
          }
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
  return find_next_entry(client, lsa, lsatype);
}

elsa_lsa elsai_get_lsa_by_type_next(elsa_client client, elsa_lsa lsa)
{
  elsa_lsatype type;

  assert(lsa->hash_entry);
  type = lsa->hash_entry->lsa.type;
  while (lsa->hash_entry->next)
    {
      lsa->hash_entry = lsa->hash_entry->next;
      /* lsa->swapped = true; - should be already! */
      if (lsa->hash_entry->lsa.type == type)
          return lsa;
    }
  lsa->hash_bin++;
  return find_next_entry(client, lsa, type);
}

/*************************************************************** IF handling */

elsa_if elsai_if_get(elsa_client client)
{
  /* Should be just 1 area, but hell.. :-) */
  elsa_if i = HEAD(client->iface_list);
  if (!NODE_VALID(i))
    i = NULL;
  /* ELSA_DEBUG("elsai_if_get %p", i); */
  return i;
}

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

  return i->iface->index;
}

uint8_t elsai_if_get_priority(elsa_client client, elsa_if i)
{
  /* XXX - someday put interface config back in? */
  return 50;
}

elsa_if elsai_if_get_next(elsa_client client, elsa_if ifp)
{
  elsa_if i = NODE_NEXT(ifp);
  if (!NODE_VALID(i))
    i = NULL;
  /* ELSA_DEBUG("elsai_if_get_next %p => %p", ifp, i); */
  return i;
}

/********************************************************* Neighbor handling */

elsa_neigh elsai_if_get_neigh(elsa_client client, elsa_if i)
{
  elsa_neigh n = HEAD(i->neigh_list);

  if (!NODE_VALID(n))
    return NULL;
  while (NODE_VALID(n) && n->state < NEIGHBOR_INIT)
    n = NODE_NEXT(n);
  if (NODE_VALID(n) && n->state >= NEIGHBOR_INIT)
    return n;
  return NULL;
}

uint32_t elsai_neigh_get_rid(elsa_client client, elsa_neigh neigh)
{
  return neigh->rid;
}

uint32_t elsai_neigh_get_iid(elsa_client client, elsa_neigh neigh)
{
  return neigh->iface_id;
}

elsa_neigh elsai_neigh_get_next(elsa_client client, elsa_neigh neigh)
{
  elsa_neigh n = NODE_NEXT(neigh);

  while (NODE_VALID(n) && n->state < NEIGHBOR_INIT)
    n = NODE_NEXT(n);
  if (NODE_VALID(n) && n->state >= NEIGHBOR_INIT)
    return n;
  return NULL;
}



/*************************************************************** Other stuff */



void elsai_lsa_originate(elsa_client client,
                         elsa_lsatype lsatype,
                         uint32_t lsid,
                         uint32_t sn,
                         const void *body, size_t body_len)
{
  struct ospf_lsa_header lsa;
  bool need_ac_save;
  void *tmp;

  tmp = mb_alloc(client->proto.pool, body_len);
  if (!tmp)
    return;
  lsa.age = 0;
#if 0
  lsa.type = ntohs(lsatype);
  lsa.id = ntohl(lsid);
  lsa.sn = ntohl(sn);
#else
  lsa.type = lsatype;
  lsa.id = lsid;
  lsa.sn = sn;
#endif /* 0 */
  lsa.rt = client->router_id;
  uint32_t dom = 0;
  lsa.length = body_len + sizeof(struct ospf_lsa_header);

  need_ac_save = client->elsa->need_ac;
  client->elsa->need_ac = false;

  ntohlsab(body, tmp, body_len);
  lsasum_calculate(&lsa, (void *)tmp);

  (void)lsa_install_new(client, &lsa, dom, tmp);

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

/***************************************************************** Debugging */

int elsai_get_log_level(void)
{
  return ELSA_DEBUG_LEVEL_DEBUG;
}


elsa_md5 elsai_md5_init(elsa_client client)
{
  elsa_md5 ctx;
  ctx = elsai_calloc(client, sizeof(*ctx));
  return ctx;
}

void elsai_md5_update(elsa_md5 md5, const unsigned char *data, int data_len)
{
  MD5Update(md5, data, data_len);
}

void elsai_md5_final(elsa_md5 md5, void *result)
{
  MD5Final(result, md5);
}
