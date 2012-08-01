/*
 * $Id: elsa_ac.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *         Benjamin Paterson <paterson.b@gmail.com>
 *
 * Created:       Wed Aug  1 14:26:19 2012 mstenber
 * Last modified: Wed Aug  1 19:01:19 2012 mstenber
 * Edit time:     127 min
 *
 */

/*
 * ELSA autoconfigure LSA handling code.
 *
 * Basic idea:
 * - Get called via elsa_ac()
 *  - Look what needs to be done within the system (using LSA DB)
 *  - Publish new AC LSA
 *
 * This is adapted from earlier work by Benjamin Paterson located at
 * https://github.com/paterben/bird-homenet
 */

#include <stdio.h>
#include "elsa_internal.h"

#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>

/* FIXME - remove the direct md5 dependency */
#include "lib/md5.h"

/* XXX - put some of this stuff in a platform dependent file. */
static bool
platform_handle_prefix(elsa_ap n, bool add)
{
  char cmd[128];

  /* And then configure it to the system */
  snprintf(cmd, sizeof(cmd), "ip -6 addr %s %x:%x:%x:%x:%x:%x:%x:%x/%d dev %s",
           add ? "add": "delete",
           ntohs(n->px.addr[0]),
           ntohs(n->px.addr[1]),
           ntohs(n->px.addr[2]),
           ntohs(n->px.addr[3]),
           ntohs(n->px.addr[4]),
           ntohs(n->px.addr[5]),
           /* 6, 7 from rid */
           n->my_rid >> 16,
           n->my_rid & 0xFFFF,
           n->px.len, n->ifname);
  return system(cmd);
}

static bool
configure_add_prefix(elsa e,
                     elsa_prefix px,
                     u32 rid,
                     u32 my_rid,
                     int pa_priority,
                     const char *ifname)
{
  elsa_ap ap;

  /* Add the prefix to the interface */
  ap = elsai_calloc(e->client, sizeof(*ap));
  if (!ap)
    return false;

  ap->px = *px;
  ap->rid = rid;
  ap->my_rid = my_rid;
  ap->pa_priority = pa_priority;
  ap->valid = 1;
  strncpy(ap->ifname, ifname, ELSA_IFNAME_LEN);
  list_add(&ap->list, &e->aps);
  if (!platform_handle_prefix(ap, true))
    {
      elsai_free(e->client, ap);
      return false;
    }
  return true;
}

static bool
configure_del_prefix(elsa e, elsa_ap ap)
{
  if (!platform_handle_prefix(ap, false))
    return false;

  /* And from the internal datastructure */
  list_del(&ap->list);
  elsai_free(e->client, ap);

  return true;
}

/* Auto-Configuration LSA Type-Length-Value (TLV) types */
#define LSA_AC_TLV_T_RHWF 1/* Router-Hardware-Fingerprint */
#define LSA_AC_TLV_T_USP  2 /* Usable Prefix */
#define LSA_AC_TLV_T_ASP  3 /* Assigned Prefix */
#define LSA_AC_TLV_T_IFAP 4 /* Interface Prefixes */

/* If x is the length of the TLV as specified in its
   Length field, returns the number of bytes used to
   represent the TLV (including type, length + padding) */
#define LSA_AC_TLV_SPACE(x) (4 + (((x + 3) / 4) * 4))

/**
 * find_next_tlv - find next TLV of specified type in AC LSA
 * @lsa: A pointer to the beginning of the body
 * @offset: Offset to the beginning of the body to start search
 * (must point to the beginning of a TLV)
 * @size: Size of the body to search
 * @type: The type of TLV to search for
 *
 * Returns a pointer to the beginning of the next TLV of specified type,
 * or null if there are no more TLVs of that type.
 * If @type is set to NULL, returns the next TLV, whatever the type.
 * Updates @offset to point to the next TLV, or to after the last TLV if
 * there are no more TLVs of the specified type.
 */
static void *
find_next_tlv(void *lsa,
              unsigned int size,
              int *offset,
              u8 type,
              u16 *read_type,
              u16 *read_size)
{
  unsigned int bound = size - 4;
  u8 *tlv = (u8 *) lsa;
  u16 *s, tlv_size, tlv_type;

  while (*offset <= bound)
  {
    s = (u16*)(tlv + *offset);
    tlv_type = ntohs(*s);
    tlv_size = ntohs(*(s+1));
    *offset += 4 + tlv_size;

    /* If too big, abort */
    if (*offset > size)
      break;

    if (!type || tlv_type == type)
      {
        if (read_type)
          *read_type = tlv_type;
        if (read_size)
          *read_size = tlv_size;
        return s;
      }
  }

  return NULL;
}

static bool
net_in_net(elsa_prefix p, elsa_prefix net)
{
  if (p->len < net->len)
    return false;
  /* p's mask has >= net's mask length */
  /* XXX - care about sub-/16 correctness */
  return memcmp(net->addr, p->addr, net->len) == 0;
}

/**
 * assignment_find - Check if we have already assigned a prefix
 * on this interface from a specified usable prefix, and return a pointer
 * to this assignment in the asp_list if it exists.
 *
 * @ifa: The current ospf_iface
 * @usp: The usable prefix
 */
static elsa_ap
assignment_find(elsa e, unsigned char *ifname, elsa_prefix usp)
{
  u32 my_rid = elsai_get_rid(e->client);
  elsa_ap ap;
  list_for_each_entry(ap, &e->aps, list)
    {
      if(ap->rid == my_rid
         && strcmp(ifname, ap->ifname) == 0
         && net_in_net(&ap->px, usp))
        {
          return ap;
        }
   }
  return NULL;
}

/**
 * random_prefix - Select a pseudorandom sub-prefix of specified length
 * @px: A pointer to the prefix
 * @pxsub: A pointer to the sub-prefix. Length field must be set.
 * @i: Number of the iteration-
 */
static void
random_prefix(elsa_prefix px, elsa_prefix pxsub, u32 rid, unsigned char *ifname, int i)
{
  struct MD5Context ctxt;
  char md5sum[16];
  int st;

  MD5Init(&ctxt);
  MD5Update(&ctxt, ifname, strlen(ifname));
  MD5Update(&ctxt, (char *)&rid, sizeof(rid));
  MD5Update(&ctxt, (char *)&i, sizeof(i));
  MD5Final(md5sum, &ctxt);

  memcpy(&pxsub->addr, md5sum, 16);

  /* FIXME - do correct bitmask operations */
  memcpy(&pxsub->addr, &px->addr, 16);
  st = px->len/16;
  memcpy(&pxsub->addr[st], md5sum, 16-2*st);
  st=pxsub->len/16;
  memset(&pxsub->addr[st], 0, 16-2*st);
}

struct ospf_lsa_ac_tlv_header
{
  u16 type;
  u16 length;
};


struct ospf_lsa_ac_tlv_v_usp /* One Usable Prefix */
{
  struct ospf_lsa_ac_tlv_header header;
  u8 pxlen;
  u8 reserved8;
  u16 reserved16;
  u32 prefix[];
};

struct ospf_lsa_ac_tlv_v_asp /* One Assigned Prefix */
{
  struct ospf_lsa_ac_tlv_header header;
  u8 pxlen;
  u8 reserved8;
  u16 reserved16;
  u32 prefix[];
};

struct ospf_lsa_ac_tlv_v_ifap /* One Interface Prefixes */
{
  struct ospf_lsa_ac_tlv_header header;
  u32 id;
  u8 pa_priority;
  u8 reserved8_1;
  u8 reserved8_2;
  u8 pa_pxlen; // must be PA_PXLEN_D or PA_PXLEN_SUB
  u32 rest[]; // Assigned Prefix TLVs
};


/* Look at the given data entry. Return true if it's desirable to
 * continue iteration.
 */

#define ITERATOR(type,name) bool (*name)(type data, void *context)

typedef ITERATOR(elsa_lsa, lsa_iterator);
typedef ITERATOR(struct ospf_lsa_ac_tlv_v_ifap *, ifap_iterator);
typedef ITERATOR(struct ospf_lsa_ac_tlv_v_asp *, asp_iterator);

static bool iterate_ac_lsa(elsa e, lsa_iterator fun, void *context)
{
  elsa_lsa lsa;

  for (lsa = elsai_get_lsa_by_type(e->client, LSA_T_AC) ; lsa ;
       lsa = elsai_get_lsa_by_type_next(e->client, lsa))
    {
      if (!fun(lsa, context))
        return false;
    }
  return true;
}

struct context_ac_ifap_struct {
  ifap_iterator ifap_iterator;
  void *ifap_context;
};

static bool iterator_ac_lsa_ifap(elsa_lsa lsa,
                                 void *context)
{
  struct context_ac_ifap_struct *ctx = context;
  unsigned int offset = 0;
  struct ospf_lsa_ac_tlv_v_ifap *tlv;
  u16 tlv_size;

  unsigned char *body;
  size_t size;

  assert(lsa);
  elsai_las_get_body(lsa, &body, &size);
  while ((tlv = find_next_tlv(body, size, &offset, LSA_AC_TLV_T_IFAP,
                              NULL, &tlv_size)))
    {
      if (tlv_size < (sizeof(*tlv)-sizeof(tlv->header)))
        break;
      ctx->ifap_iterator(tlv, ctx->ifap_context);
    }
  return true;
}

static bool iterate_ac_lsa_ifap(elsa e, ifap_iterator fun, void *context)
{
  struct context_ac_ifap_struct ctx = { .ifap_iterator = fun,
                                        .ifap_context = context};
  return iterate_ac_lsa(e, iterator_ac_lsa_ifap, &ctx);
}

struct context_ac_ifap_asp_struct {
  asp_iterator asp_iterator;
  void *asp_context;
};

static bool iterator_ac_lsa_ifap_asp(struct ospf_lsa_ac_tlv_v_ifap *ifap,
                                     void *context)
{
  struct context_ac_ifap_asp_struct *ctx = context;
  unsigned int offset = 0;
  struct ospf_lsa_ac_tlv_v_asp *tlv;
  u16 tlv_size;
  int size =
    sizeof(struct ospf_lsa_ac_tlv_header) + ntohs(ifap->header.length) -
    offsetof(struct ospf_lsa_ac_tlv_v_ifap, rest);
  while ((tlv = find_next_tlv(ifap->rest,
                              size,
                              &offset, LSA_AC_TLV_T_ASP,
                              NULL, &tlv_size)))
    {
      if (tlv_size < sizeof(*tlv))
        break;
      ctx->asp_iterator(tlv, ctx->asp_context);
    }
  return true;
}

  
static bool iterate_ac_lsa_ifap_asp(elsa e, asp_iterator fun, void *context)
{
  struct context_ac_ifap_asp_struct ctx = { .asp_iterator = fun,
                                            .asp_context = context};
  return iterate_ac_lsa_ifap(e, iterator_ac_lsa_ifap_asp, &ctx);
}


#define PARSE_LSA_AC_USP_START(usp,en)                                                    \
if((en = ospf_hash_find_ac_lsa_first(oa->po->gr, oa->areaid)) != NULL)                    \
{                                                                                         \
  do {                                                                                    \
    if(ospf_lsa_ac_is_reachable(po, en))                                                  \
    {                                                                                     \
      struct ospf_lsa_ac *tlv;                                                            \
      unsigned int size = en->lsa.length - sizeof(struct ospf_lsa_header);                \
      unsigned int offset = 0;                                                            \
      while((tlv = find_next_tlv(en->lsa_body, &offset, size, LSA_AC_TLV_T_USP)) != NULL) \
      {                                                                                   \
        usp = (struct ospf_lsa_ac_tlv_v_usp *)(tlv->value);

#define PARSE_LSA_AC_USP_END(en) } } } while((en = ospf_hash_find_ac_lsa_next(en)) != NULL); }
#define PARSE_LSA_AC_USP_BREAKIF(x,en) if(x) break; } if(x) break; } } while((en = ospf_hash_find_ac_lsa_next(en)) != NULL); }


/**
 * in_use - Determine if a prefix is already in use
 * @px: The prefix of interest
 * @used: A list of struct prefix_node
 *
 * This function returns 1 if @px is a sub-prefix or super-prefix
 * of any of the prefixes in @used, 0 otherwise.
 */
static bool
in_use(elsa e, elsa_prefix px)
{
  elsa_ap ap;
  u32 my_rid = elsai_get_rid(e->client);

  /* Local state */
  list_for_each_entry(ap, &e->aps, list)
    if (net_in_net(&ap->px, px) || net_in_net(px, &ap->px))
      return true;

  PARSE_LSA_AC_IFAP_START(ifap, en)
  {
    if(en->lsa.rt != my_id) // don't check our own LSAs
    {
      PARSE_LSA_AC_ASP_START(asp, ifap)
      {
        ip_addr addr;
        unsigned int len;
        u8 pxopts;
        u16 rest;

        lsa_get_ipv6_prefix((u32 *)(asp) , &addr, &len, &pxopts, &rest);
        // test if assigned prefix is part of current usable prefix
        if(net_in_net(addr, len, usp_addr, usp_len))
        {
          /* add prefix to list of used prefixes */
          pxn = mb_alloc(p->pool, sizeof(struct prefix_node));
          add_tail(used, NODE pxn);
          pxn->px.addr = addr;
          pxn->px.len = len;
          pxn->pa_priority = ifap->pa_priority;
          pxn->rid = en->lsa.rt;

          if(ifa->pa_pxlen == PA_PXLEN_D)
          {
            // test if assigned prefix is stealable
            if((ifap->pa_priority < lowest_pa_priority
                || (ifap->pa_priority == lowest_pa_priority && ifap->pa_pxlen < lowest_pa_pxlen))
               && (!is_reserved_prefix(addr, len, usp_addr, usp_len)))
            {
              *steal_addr = ipa_and(addr,ipa_mkmask(PA_PXLEN_D));
              *steal_len = PA_PXLEN_D;
              lowest_pa_priority = ifap->pa_priority;
              lowest_pa_pxlen = ifap->pa_pxlen;
              lowest_rid = en->lsa.rt;
              *found_steal = 1;
            }
          }
        }
      }
      PARSE_LSA_AC_ASP_END;
    }
  }
  PARSE_LSA_AC_IFAP_END(en);

  return 0;
}

/**
 * choose_prefix - Choose a prefix of specified length from
 * a usable prefix and a list of sub-prefixes in use
 * @pxu: The usable prefix
 * @px: A pointer to the prefix structure. Length must be set.
 * @used: The list of sub-prefixes already in use
 *
 * This function stores a unused prefix of specified length from
 * the usable prefix @pxu, and returns PXCHOOSE_SUCCESS,
 * or stores IPA_NONE into @px->ip and returns PXCHOOSE_FAILURE if
 * all prefixes are in use.
 *
 * This function will never select the numerically highest /64 prefix
 * in the usable prefix (it is considered reserved).
 */
static int
choose_prefix(elsa_prefix pxu, elsa_prefix px, list used, u32 rid, struct ospf_iface *ifa)
{
  /* (Stupid) Algorithm:
     - try a random prefix until success or 10 attempts have passed
     - if failure, do:
       * set looped to 0
       * store prefix in start_prefix
       * while looped is 0 or prefix is strictly smaller than start_prefix, do:
         * if prefix is not in usable prefix range, set to
           lowest prefix of range and set looped to 1
         * if prefix is available, return
         * find one of the used prefixes which contains/is contained in this prefix then
           increment prefix to the first prefix of correct length that
           is not covered by that used prefix / does not cover that used prefix */
  elsa_ap n;
  int looped;
  struct prefix start_prefix;

  int i;
  for(i=0;i<10;i++)
  {
    random_prefix(pxu, px, rid, ifa, i);
    if(!in_use(e, px))
        return PXCHOOSE_SUCCESS;
  }

  looped = 0;
  start_prefix = *px;
  while(looped == 0 || ipa_compare(px->addr, start_prefix.addr) < 0)
  {
    if(!net_in_net(px->addr, px->len, pxu->addr, pxu->len))
    {
      px->addr = pxu->addr;
      looped = 1;
    }

    if(!in_use(e, px))
    {
      return PXCHOOSE_SUCCESS;
    }

    WALK_LIST(n, used)
    {
      if(net_in_net(px->addr, px->len, n->px.addr, n->px.len)
         || net_in_net(n->px.addr, n->px.len, px->addr, px->len))
      {
        next_prefix(px, &n->px);
        break;
      }
    }
  }
  return PXCHOOSE_FAILURE;
}

/**
 * ospf_pxassign_area - Run prefix assignment algorithm for
 * usable prefixes advertised by AC LSAs in a specific area.
 *
 * @oa: The area to search for LSAs in. Note that the algorithm
 * may impact interfaces that are not in this area.
 */
void
ospf_pxassign_area(struct ospf_area *oa)
{
  struct proto *p = &oa->po->proto;
  struct proto_ospf *po = oa->po;
  struct top_hash_entry *en;
  struct ospf_iface *ifa;
  elsa_ap asp, *aspn;
  struct ospf_lsa_ac_tlv_v_usp *usp;
  struct ospf_iface_prefixes *ip, *ipn;
  int change = 0;

  //OSPF_TRACE(D_EVENTS, "Starting prefix assignment algorithm for AC LSAs in area %R", oa->areaid);

  /* mark all this area's iface's assignments as invalid */
  WALK_LIST(ifa, po->iface_list)
  {
    if(ifa->oa == oa)
    {
      WALK_LIST(asp, ifa->asp_list)
      {
        if (asp->valid)
          asp->valid--;
      }
    }
  }

  // perform the prefix assignment algorithm on each (USP, iface) tuple
  PARSE_LSA_AC_USP_START(usp,en)
  {
    WALK_LIST(ifa, po->iface_list)
    {
      if(ifa->oa == oa)
      {
        change |= ospf_pxassign_usp_ifa(ifa, (struct ospf_lsa_ac_tlv_v_usp *)(usp));
      }
    }
  }
  PARSE_LSA_AC_USP_END(en);

  /* remove all this area's iface's invalid assignments */
  WALK_LIST(ifa, po->iface_list)
  {
    if(ifa->oa == oa)
    {
      WALK_LIST(asp, ifa->asp_list)
      {
        if(!asp->valid)
          {
            if(asp->rid == po->router_id)
              change = 1;
            OSPF_TRACE(D_EVENTS, "Interface %s: assignment %I/%d removed as invalid", ifa->iface->name, asp->px.addr, asp->px.len);
            configure_del_prefix(e, asp);
          }
      }
    }
  }

  WALK_LIST_DELSAFE(ip, ipn, po->ip_list)
    {
      if (--(ip->valid) == 0)
        {
          WALK_LIST_DELSAFE(asp, aspn, ip->asp_list)
            {
              OSPF_TRACE(D_EVENTS, "Interface %s: assignment %I/%d removed (flip-flop, never reappeared)", asp->ifname, asp->px.addr, asp->px.len);
              configure_del_prefix(e, asp);
            }
          rem_node(NODE ip);
          mb_free(ip);
        }
    }

  if(change)
  {
     schedule_ac_lsa(oa);
  }
}

/** ospf_pxassign_usp_ifa - Main prefix assignment algorithm
 *
 * @ifa: The Current Interface
 * @usp: The Current Usable Prefix
 */
int
ospf_pxassign_usp_ifa(struct ospf_iface *ifa, struct ospf_lsa_ac_tlv_v_usp *cusp)
{
  struct top_hash_entry *en;
  struct ospf_area *oa = ifa->oa;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;
  //struct ospf_neighbor *neigh;
  //struct ospf_usp *usp;
  struct ospf_iface *ifa2;
  struct ospf_lsa_ac_tlv_v_usp *usp2;
  struct ospf_lsa_ac_tlv_v_asp *asp;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  struct ospf_neighbor *neigh;
  elsa_ap pxn, *n, *self_r_px = NULL;
  //timer *pxassign_timer;
  ip_addr usp_addr, usp2_addr, neigh_addr, neigh_r_addr;
  unsigned int usp_len, usp2_len, neigh_len, neigh_r_len;
  u8 usp_pxopts, usp2_pxopts, neigh_pxopts;
  u16 usp_rest, usp2_rest, neigh_rest;
  int change = 0;

  lsa_get_ipv6_prefix((u32 *)cusp, &usp_addr, &usp_len, &usp_pxopts, &usp_rest);

  //OSPF_TRACE(D_EVENTS, "Starting prefix assignment algorithm for prefix %I/%d", ip, pxlen);

  /* 8.5.0 */
  PARSE_LSA_AC_USP_START(usp2, en)
  {
    lsa_get_ipv6_prefix((u32 *)usp2, &usp2_addr, &usp2_len, &usp2_pxopts, &usp2_rest);
    if(net_in_net(usp_addr, usp_len, usp2_addr, usp2_len) && (!ipa_equal(usp_addr, usp2_addr) || usp_len != usp2_len))
      return change;
  }
  PARSE_LSA_AC_USP_END(en);

  /* 8.5.1 */
  /* FIXME I think the draft should say "active neighbors" (state >= Init), that's what I suppose */
  /*int have_neigh = 0;
  WALK_LIST(neigh, ifa->neigh_list)
  {
    if(neigh->state >= NEIGHBOR_INIT)
      have_neigh = 1;
  }*/

  /* 8.5.2a and 8.5.2b */
  int have_highest_link_pa_priority = 0;
  int have_highest_link_pa_pxlen = 0; // only relevant if we have highest priority
  u8 highest_link_pa_priority = 0;
  u8 highest_link_pa_pxlen = 0;
  WALK_LIST(neigh, ifa->neigh_list)
  {
    if(neigh->state >= NEIGHBOR_INIT)
    {
      PARSE_LSA_AC_IFAP_ROUTER_START(neigh->rid, ifap, en)
      {
        if(ifap->id == neigh->iface_id)
        {
          // store for future reference
          neigh->pa_priority = ifap->pa_priority;
          neigh->pa_pxlen = ifap->pa_pxlen;

          if(ifap->pa_priority > highest_link_pa_priority)
          {
            highest_link_pa_priority = ifap->pa_priority;
            highest_link_pa_pxlen = ifap->pa_pxlen;
          }
          if(ifap->pa_priority == highest_link_pa_priority && ifap->pa_pxlen > highest_link_pa_pxlen)
            highest_link_pa_pxlen = ifap->pa_pxlen;
        }
      }
      PARSE_LSA_AC_IFAP_ROUTER_END(en);
    }
  }
  if(highest_link_pa_priority < ifa->pa_priority
     || (highest_link_pa_priority == ifa->pa_priority && highest_link_pa_pxlen <= ifa->pa_pxlen))
  {
    highest_link_pa_pxlen = ifa->pa_pxlen;
    have_highest_link_pa_pxlen = 1;
  }
  if(highest_link_pa_priority <= ifa->pa_priority)
  {
    highest_link_pa_priority = ifa->pa_priority;
    have_highest_link_pa_priority = 1;
  }

  /* 8.5.2c */
  int have_highest_link_rid = 1; // only relevant if have highest priority + pa_pxlen
  WALK_LIST(neigh, ifa->neigh_list)
  {
    if(neigh->state >= NEIGHBOR_INIT
       && neigh->pa_priority == highest_link_pa_priority
       && neigh->pa_pxlen == highest_link_pa_pxlen
       && neigh->rid > po->router_id)
    {
      have_highest_link_rid = 0;
      break;
    }
  }

  /* 8.5.2d */
  int assignment_found = 0;
  u32 neigh_rid = 0; // RID of responsible neighbor, if any
  WALK_LIST(neigh, ifa->neigh_list)
  {
    if(neigh->state >= NEIGHBOR_INIT
       && neigh->pa_priority == highest_link_pa_priority
       && neigh->pa_pxlen == highest_link_pa_pxlen
       && neigh->rid > neigh_rid)
    {
      PARSE_LSA_AC_IFAP_ROUTER_START(neigh->rid, ifap, en)
      {
        if(ifap->id == neigh->iface_id)
        {
          PARSE_LSA_AC_ASP_START(asp, ifap)
          {
            lsa_get_ipv6_prefix((u32 *)(asp), &neigh_addr, &neigh_len, &neigh_pxopts, &neigh_rest);
            if(net_in_net(neigh_addr, neigh_len, usp_addr, usp_len))
            {
              /* a prefix has already been assigned by a neighbor to the link */
              /* we're not sure it is responsible for the link yet, so we store
                 the assigned prefix and keep looking at other neighbors with
                 same priority/pa_pxlen and higher RID */
              neigh_r_addr = neigh_addr;
              neigh_r_len = neigh_len;
              neigh_rid = neigh->rid;
              assignment_found = 1;
              break;
            }
          }
          PARSE_LSA_AC_ASP_BREAKIF(assignment_found);
        }
      }
      PARSE_LSA_AC_IFAP_ROUTER_BREAKIF(assignment_found, en);
    }
  }

  /* 8.5.2e */
  int have_highest_link_assignment = 0;
  if(have_highest_link_pa_priority
     && have_highest_link_pa_pxlen
     && po->router_id > neigh_rid)
  {
    struct prefix usp_px;
    usp_px.addr = usp_addr;
    usp_px.len = usp_len;
    self_r_px = assignment_find(ifa, &usp_px);
    if(self_r_px)
      have_highest_link_assignment = 1;
  }

  /* 8.5.3 */
  // exactly one of the following will be executed:
  // step 4 will be executed if:
  //   have_highest_link_assignment
  // step 5 will be executed if:
  //   !have_highest_link_assignment && assignment_found
  // step 6 will be executed if:
  //   !have_highest_link_assignment && !assignment_found && have_highest_link_pa_priority && have_highest_link_pa_pxlen && have_highest_link_rid
  if(!have_highest_link_assignment && !assignment_found && (!have_highest_link_pa_priority || !have_highest_link_pa_pxlen || !have_highest_link_rid))
    return change; // go to next interface

  /* 8.5.4 */
  // we already have an assignment but must check whether it is valid and whether there is better
  unsigned int deassigned_prefix = 0; // whether we had to remove our own assignment. Causes jump to step 8.5.6.
  if(have_highest_link_assignment)
  {
    PARSE_LSA_AC_IFAP_START(ifap, en)
    {
      if(en->lsa.rt != po->router_id // don't check our own LSAs
         && (ifap->pa_priority > highest_link_pa_priority
             || (ifap->pa_priority == highest_link_pa_priority && ifap->pa_pxlen > highest_link_pa_pxlen)
             || (ifap->pa_priority == highest_link_pa_priority && ifap->pa_pxlen == highest_link_pa_pxlen && en->lsa.rt > po->router_id)))
      {
        PARSE_LSA_AC_ASP_START(asp, ifap)
        {
          ip_addr addr;
          unsigned int len;
          u8 pxopts;
          u16 rest;

          lsa_get_ipv6_prefix((u32 *)(asp), &addr, &len, &pxopts, &rest);

          // test if assigned prefix collides with our assignment
          if(net_in_net(addr, len, self_r_px->px.addr, self_r_px->px.len) || net_in_net(self_r_px->px.addr, self_r_px->px.len, addr, len))
          {
            OSPF_TRACE(D_EVENTS, "Interface %s: assignment %I/%d collides with %I/%d, removing", ifa->iface->name, self_r_px->px.addr, self_r_px->px.len, addr, len);
            configure_del_prefix(e, self_r_px);
            deassigned_prefix = 1;
            change = 1;
            break;
          }
        }
        PARSE_LSA_AC_ASP_BREAKIF(deassigned_prefix);
      }
    }
    PARSE_LSA_AC_IFAP_BREAKIF(deassigned_prefix, en);

    // also check other assignments for which we are responsible to see if this one is valid.
    // This should be useless: we should never have made a colliding assignment
    // without deleting this one in the first place
    if(!deassigned_prefix)
    {
      WALK_LIST(ifa2, po->iface_list)
      {
        if(ifa->oa == oa)
        {
          WALK_LIST(n, ifa2->asp_list)
          {
            if(n->rid == po->router_id
               && (ifa2->pa_priority > highest_link_pa_priority
                   || (ifa2->pa_priority == highest_link_pa_priority && ifa2->pa_pxlen >= highest_link_pa_pxlen)))
            {
              if((net_in_net(n->px.addr, n->px.len, self_r_px->px.addr, self_r_px->px.len) || net_in_net(self_r_px->px.addr, self_r_px->px.len, n->px.addr, n->px.len))
                 && (!ipa_equal(self_r_px->px.addr, n->px.addr) || self_r_px->px.len != n->px.len))
              {
                die("Bug in prefix assignment algorithm: forgot to remove a prefix when assigning new one");
                /*OSPF_TRACE(D_EVENTS, "Interface %s: own assignment %I/%d collides with %I/%d, removing", ifa->iface->name, self_r_px->px.addr, self_r_px->px.len, addr, len);
                rem_node(NODE self_r_px);
                mb_free(self_r_px);
                deassigned_prefix = 1;
                change = 1;
                // FIXME deassign prefix from interface
                break;*/
              }
            }
          }
          // if(deassigned_prefix) break;
        }
      }
    }

  /* 8.5.5 */
  // we must check whether we are aware of someone else's assignment
  if(!have_highest_link_assignment && assignment_found)
  {
    int found = 0; // whether assignment is already in the ifa's asp_list
    WALK_LIST(n,ifa->asp_list)
    {
      if(ipa_equal(n->px.addr, neigh_r_addr) && n->px.len == neigh_r_len
         && n->rid == neigh_rid && n->pa_priority == highest_link_pa_priority)
      {
        found = 1;
        n->valid = 1;
      }
    }

    // if it's not already there, we must run some extra checks to see if we can assign it.
    // parse all interface's asp_lists twice: once to determine if the new assignment takes
    // priority, second to remove all colliding assignments if it does.
    // cases a colliding existing assignment wins and new one must be refused:
    //   existing has a strictly higher pa_priority
    //   existing has the same pa_priority and a strictly longer prefix
    //   existing has the same pa_priority, same prefix and strictly higher RID
    int refused = 0;
    int collision_found = 0;
    if(!found)
    {
      WALK_LIST(ifa2, po->iface_list)
      {
        if(ifa2->oa == oa)
        {
          WALK_LIST(n, ifa2->asp_list)
          {
            if(net_in_net(n->px.addr, n->px.len, neigh_r_addr, neigh_r_len)
               || net_in_net(neigh_r_addr, neigh_r_len, n->px.addr, n->px.len))
            {
              collision_found = 1;
              if(n->pa_priority > highest_link_pa_priority
                 || (n->pa_priority == highest_link_pa_priority && n->px.len > neigh_r_len)
                 || (n->pa_priority == highest_link_pa_priority && n->px.len == neigh_r_len && n->rid > neigh_rid))
              {
                refused = 1;
                OSPF_TRACE(D_EVENTS, "Interface %s: Refused %R's assignment %I/%d with priority %d, we have interface %s router %R assignment %I/%d with priority %d",
                                     ifa->iface->name, neigh_rid, neigh_r_addr, neigh_r_len, highest_link_pa_priority, ifa2->iface->name, n->rid, n->px.addr, n->px.len, n->pa_priority);
                break;
                // we will have no assignment on this interface, but we don't know who's responsible.
                // if the neighbor is ill-intentioned and never removes his assignment,
                // no prefix will ever be assigned on this interface.
                // it would be possible to run some additional steps to see if we are responsible here.
                // under normal conditions, the neighbor will eventually remove his assignment.
              }
            }
          }
          if(refused) break;
        }
      }
    }
    if(!refused && collision_found)
    {
      // delete all colliding assignments on interfaces
      WALK_LIST(ifa2, po->iface_list)
      {
        if(ifa2->oa == oa)
        {
          WALK_LIST_DELSAFE(n, pxn, ifa2->asp_list)
          {
            if(net_in_net(n->px.addr, n->px.len, neigh_r_addr, neigh_r_len)
               || net_in_net(neigh_r_addr, neigh_r_len, n->px.addr, n->px.len))
            {
              OSPF_TRACE(D_EVENTS, "Interface %s: To add %R's assignment %I/%d with priority %d, must delete interface %s router %R assignment %I/%d with priority %d",
                                   ifa->iface->name, neigh_rid, neigh_r_addr, neigh_r_len, highest_link_pa_priority, ifa2->iface->name, n->rid, n->px.addr, n->px.len, n->pa_priority);
              if(n->rid == po->router_id)
                change = 1;
              configure_del_prefix(e, n);
            }
          }
        }
      }
    }

    if(!found && !refused)
    {
      OSPF_TRACE(D_EVENTS, "Interface %s: Adding %R's assignment %I/%d with priority %d", ifa->iface->name, neigh_rid, neigh_r_addr, neigh_r_len, highest_link_pa_priority);
      configure_add_prefix(neigh_r_addr, neigh_r_len, neigh_rid, po->router_id, highest_link_pa_priority, ifa);
    }
  }

  /* 8.5.6 */
  // we must assign a new prefix
  if(deassigned_prefix
     || (!have_highest_link_assignment && !assignment_found && have_highest_link_pa_priority && have_highest_link_pa_pxlen && have_highest_link_rid))
  {
    list used; /* list of elsa_ap */
    init_list(&used);
    ip_addr steal_addr;
    unsigned int steal_len;
    unsigned int found_steal = 0;
    unsigned int pxchoose_success = 0;

    /* 8.5.6a */
    // find all used prefixes in LSADB and our own interface's asp_lists
    find_used(ifa, usp_addr, usp_len, &used, &steal_addr, &steal_len, &found_steal);

    /* 8.5.6b */
    // see if we can find a prefix in memory that is unused
    try_reuse(ifa, usp_addr, usp_len, &used, &pxchoose_success, &change, NULL);

    /* 8.5.6c */
    // see if we can find an unused prefix
    if(!pxchoose_success)
      try_assign_unused(ifa, usp_addr, usp_len, &used, &pxchoose_success, &change, NULL);

    /* 8.5.6d */
    // try to steal a /64
    if(!pxchoose_success && ifa->pa_pxlen == PA_PXLEN_D && found_steal)
    {
      try_assign_specific(ifa, usp_addr, usp_len, &steal_addr, &steal_len, &pxchoose_success, &change, NULL);
    }

    /* 8.5.6f */
    if(!pxchoose_success)
      OSPF_TRACE(D_EVENTS, "Interface %s: No prefixes left to assign from prefix %I/%d.", ifa->iface->name, usp_addr, usp_len);

    WALK_LIST_DELSAFE(n, pxn, used)
    {
      rem_node(NODE n);
      mb_free(n);
    }
  }

  return change;
}

/**
 * find_used - Find all already used prefixes
 *
 * Updates list of used prefixes @used.
 * Also updates @steal_addr, @steal_len, @found_steal.
 */
static void
find_used(struct ospf_iface *ifa, ip_addr usp_addr, unsigned int usp_len, list *used, ip_addr *steal_addr, unsigned int *steal_len,
          unsigned int *found_steal)
{
  struct ospf_area *oa = ifa->oa;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;
  struct top_hash_entry *en;
  elsa_ap n, *pxn;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  struct ospf_lsa_ac_tlv_v_asp *asp;
  struct ospf_iface *ifa2;

  u8 lowest_pa_priority, lowest_pa_pxlen;
  u32 lowest_rid;

  lowest_pa_priority = ifa->pa_priority;
  lowest_pa_pxlen = ifa->pa_pxlen;
  lowest_rid = po->router_id;


  /* we also check our own interfaces for assigned prefixes for which we are responsible */
  WALK_LIST(ifa2, po->iface_list)
  {
    if(ifa2->oa == oa)
    {
      WALK_LIST(n, ifa2->asp_list)
      {
        if(n->rid == po->router_id && net_in_net(n->px.addr, n->px.len, usp_addr, usp_len))
        {
          /* add prefix to list of used prefixes */
          pxn = mb_alloc(p->pool, sizeof(struct prefix_node));
          add_tail(used, NODE pxn);
          pxn->px.addr = n->px.addr;
          pxn->px.len = n->px.len;
          pxn->rid = n->rid;
          pxn->pa_priority = ifa2->pa_priority;

          if(ifa->pa_pxlen == PA_PXLEN_D)
          {
            // test if assigned prefix is stealable
            if((ifa2->pa_priority < lowest_pa_priority
                || (ifa2->pa_priority == lowest_pa_priority && ifa2->pa_pxlen < lowest_pa_pxlen))
               && (!is_reserved_prefix(pxn->px.addr, pxn->px.len, usp_addr, usp_len)))
            {
              *steal_addr = ipa_and(n->px.addr,ipa_mkmask(PA_PXLEN_D));
              *steal_len = PA_PXLEN_D;
              lowest_pa_priority = ifa2->pa_priority;
              lowest_pa_pxlen = ifa2->pa_pxlen;
              lowest_rid = po->router_id;
              *found_steal = 1;
            }
          }
        }
      }
    }
  }
}

/**
 * try_reuse - Try to reuse an unused prefix of specified @length in memory
 */
static void
try_reuse(struct ospf_iface *ifa, ip_addr usp_addr, unsigned int usp_len, list *used,
            unsigned int *pxchoose_success, unsigned int *change, elsa_ap self_r_px)
{
  // FIXME implement
}

/**
 * try_assign_unused - Try to assign an unused prefix of specified @length.
 * @self_r_px: if this is not set to NULL and a successful assignment takes place,
 * removes this prefix (this must be the reserved prefix).
 */
static void
try_assign_unused(struct ospf_iface *ifa, ip_addr usp_addr, unsigned int usp_len, list *used, unsigned int *pxchoose_success,
                  unsigned int *change, elsa_ap self_r_px)
{
  struct proto_ospf *po = ifa->oa->po;
  struct proto *p = &po->proto;
  struct prefix px, pxu;

  px.addr = IPA_NONE;
  px.len = ifa->pa_pxlen;
  if(ifa->pa_pxlen == PA_PXLEN_D)
  {
    pxu.addr = usp_addr;
    pxu.len = usp_len;
  }
  else if(ifa->pa_pxlen == PA_PXLEN_SUB)
  {
    if(compute_reserved_prefix(&pxu.addr, &pxu.len, &usp_addr, &usp_len) == -1)
      die("bug in prefix assignment algorithm: usable prefix too long");
  }
  else die("bug in prefix assignment algorithm: trying to assign nonstandard length");

  switch(choose_prefix(&pxu, &px, *used, po->router_id, ifa))
  {
    case PXCHOOSE_SUCCESS:
      if(self_r_px)
      {
        // delete the reserved /64 prefix that is going to be replaced
        OSPF_TRACE(D_EVENTS, "Interface %s: Replacing prefix %I/%d with prefix %I/%d from usable prefix %I/%d", ifa->iface->name, self_r_px->px.addr, self_r_px->px.len, px.addr, px.len, usp_addr, usp_len);
        configure_del_prefix(e, self_r_px);
      }
      else {
        OSPF_TRACE(D_EVENTS, "Interface %s: Assigned prefix %I/%d from usable prefix %I/%d", ifa->iface->name, px.addr, px.len, usp_addr, usp_len);
      }
      *change = 1;
      *pxchoose_success = 1;
      configure_add_prefix(px.addr, px.len, po->router_id, po->router_id, ifa->pa_priority, ifa);
      break;

    case PXCHOOSE_FAILURE:
      //log(L_WARN "%s: No prefixes left to assign to interface %s from prefix %I/%d.", p->name, ifa->iface->name, usp_addr, usp_len);
      break;
  }
}

/**
 * try_assign_specific - Try to assign a specific prefix, used or not.
 * Only check that the assignment is legal
 * when considering the interface's priority and pa_pxlen.
 * If @self_r_px is not NULL and an assignment can be made,
 * the @self_r_px assignment is removed.
 */
static void
try_assign_specific(struct ospf_iface *ifa, ip_addr usp_addr, unsigned int usp_len, ip_addr *spec_addr, unsigned int *spec_len,
                    unsigned int *pxchoose_success, unsigned int *change, elsa_ap self_r_px)
{
  struct ospf_area *oa = ifa->oa;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  struct ospf_lsa_ac_tlv_v_asp *asp;
  struct top_hash_entry *en;
  struct ospf_iface *ifa2;
  elsa_ap n, pxn;
  unsigned int can_assign = 1;

  // we need to check that no one else has already assigned the specific prefix.
  PARSE_LSA_AC_IFAP_START(ifap, en)
  {
    if(en->lsa.rt != po->router_id // don't check our own LSAs
       && (ifap->pa_priority > ifa->pa_priority
           || (ifap->pa_priority == ifa->pa_priority && ifap->pa_pxlen >= ifa->pa_pxlen)))
    {
      PARSE_LSA_AC_ASP_START(asp, ifap)
      {
        ip_addr addr;
        unsigned int len;
        u8 pxopts;
        u16 rest;

        lsa_get_ipv6_prefix((u32 *)(asp) , &addr, &len, &pxopts, &rest);
        if(net_in_net(addr, len, *spec_addr, *spec_len)
           || net_in_net(*spec_addr, *spec_len, addr, len))
          can_assign = 0;
      }
      PARSE_LSA_AC_ASP_BREAKIF(!can_assign);
    }
  }
  PARSE_LSA_AC_IFAP_BREAKIF(!can_assign, en);

  // we also need to check that we have not already assigned
  // a colliding prefix ourselves
  if(can_assign)
  {
    WALK_LIST(ifa2, po->iface_list)
    {
      if(ifa2->oa == oa)
      {
        WALK_LIST(n, ifa2->asp_list)
        {
          if(n->rid == po->router_id
             && (n->pa_priority > ifa->pa_priority
                 || (n->pa_priority == ifa->pa_priority && n->px.len >= ifa->pa_pxlen)))
          {
            if(net_in_net(n->px.addr, n->px.len, *spec_addr, *spec_len)
               || net_in_net(*spec_addr, *spec_len, n->px.addr, n->px.len))
            {
                can_assign = 0;
            }
          }
        }
      }
    }
  }

  if(can_assign)
  {
    // delete colliding assignments from any other interfaces
    WALK_LIST(ifa2, po->iface_list)
    {
      if(ifa2->oa == oa)
      {
        WALK_LIST_DELSAFE(n, pxn, ifa2->asp_list)
        {
          if((net_in_net(n->px.addr, n->px.len, *spec_addr, *spec_len)
              || net_in_net(*spec_addr, *spec_len, n->px.addr, n->px.len))
             && (!self_r_px
                 || (!ipa_equal(self_r_px->px.addr, n->px.addr)
                     || self_r_px->pa_priority != n->pa_priority
                     || self_r_px->px.len != n->px.len
                     || n->rid != po->router_id)))
                 // self_r_px will be removed in next step if it exists
          {
            OSPF_TRACE(D_EVENTS, "Interface %s: Trying to assign %I/%d, must remove %I/%d from interface %s", ifa->iface->name, *spec_addr, *spec_len, n->px.addr, n->px.len, ifa2->iface->name);
            if(n->rid == po->router_id)
              *change = 1;
            configure_del_prefix(e, n);
          }
        }
      }
    }

    // finally, do the assignment
    if(self_r_px)
    {
      OSPF_TRACE(D_EVENTS, "Interface %s: Replacing prefix %I/%d with prefix %I/%d from usable prefix %I/%d", ifa->iface->name, self_r_px->px.addr, self_r_px->px.len, *spec_addr, *spec_len, usp_addr, usp_len);
      configure_del_prefix(e, self_r_px);
    }
    else {
      OSPF_TRACE(D_EVENTS, "Interface %s: Assigned prefix %I/%d from usable prefix %I/%d", ifa->iface->name, *spec_addr, *spec_len, usp_addr, usp_len);
    }
    *change = 1;
    *pxchoose_success = 1;
    configure_add_prefix(*spec_addr, *spec_len, po->router_id, po->router_id, ifa->pa_priority, ifa);
  }
}




void elsa_ac_init(elsa elsa)
{
  INIT_LIST_HEAD(&elsa->local_prefixes);
}

void elsa_ac_uninit(elsa elsa)
{
}

void elsa_ac(elsa elsa)
{
}
