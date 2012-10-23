/*
 * $Id: elsa_platform.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:14:38 2012 mstenber
 * Last modified: Mon Oct 22 17:14:08 2012 mstenber
 * Edit time:     111 min
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

/* Sigh.. cut-n-pate from rt.c */
#ifdef OSPFv2
#define ipa_from_rid(x) _MI(x)
#else /* OSPFv3 */
#define ipa_from_rid(x) _MI(0,0,0,x)
#endif

void elsai_route_to_rid(elsa_client client, uint32_t rid,
                        char **output_nh, char **output_if)
{
  /* There seems to be two (bad) solutions to this. First one doesn't
     work without changes to rt.c, second one works but shouldn't
     (according to code comments). */
#if 0
  /* This uses rt.c internal data structures, and can't work -
     the structures are cleared at end of calculation. */
  struct ospf_area *area = ospf_find_area(client, 0);
  ip_addr addr = ipa_from_rid(rid);
  ort *r = (ort *)fib_find(&area->rtr, &addr, MAX_PREFIX_LENGTH);
  /* ~maximum is 2+1 * 8 = ~24 bytes */
  static char nh_buf[25];

  *output_nh = NULL;
  *output_if = NULL;
  ELSA_DEBUG("elsai_route_to_rid %x got %p %p %p",
             rid,
             r,
             r ? r->n.nhs : NULL,
             r && r->n.nhs ? r->n.nhs->iface : NULL);
  if (r && r->n.nhs && r->n.nhs->iface)
    {
      ip_ntop(r->n.nhs->gw, nh_buf);
      *output_nh = nh_buf;
      *output_if = r->n.nhs->iface->name;
    }
#else
  /* This uses topology.[ch] API, which is reasonable choice, but
     code comment says ->nhs is valid only during SPF calculation. This
     isn't, strictly speaking, true, at the moment. */
  struct top_hash_entry *en = ospf_hash_find_rt(client->gr, 0, rid);
  /* ~maximum is 2+1 * 8 = ~24 bytes */
  static char nh_buf[25];

  *output_nh = NULL;
  *output_if = NULL;
  ELSA_DEBUG("elsai_route_to_rid %x got %p %p %p",
             rid,
             en,
             en ? en->nhs : NULL,
             en && en->nhs ? en->nhs->iface : NULL);
  if (en && en->nhs && en->nhs->iface)
    {
      ip_ntop(en->nhs->gw, nh_buf);
      *output_nh = nh_buf;
      *output_if = en->nhs->iface->name;
    }
#endif /* 0 */
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
#ifdef OSPFv3
  return neigh->iface_id;
#else
  return 0;
#endif
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
                         const unsigned char *body, size_t body_len)
{
  struct ospf_lsa_header lsa;
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

  ntohlsab((void *)body, tmp, body_len);
  lsasum_calculate(&lsa, (void *)tmp);

  (void)lsa_install_new(client, &lsa, dom, tmp);

  ospf_lsupd_flood(client, NULL, NULL, &lsa, dom, 1);
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

/********************************************************* Platform-specific */

elsa_lsa elsa_platform_wrap_lsa(elsa_client client,
                                struct top_hash_entry *lsa)
{
  int idx = client->elsa->platform.last_lsa++
    % SUPPORTED_SIMULTANEOUS_LSA_ITERATIONS;
  elsa_lsa l = &client->elsa->platform.lsa[idx];
  l->swapped = false;
  l->hash_entry = lsa;
  return l;
}
