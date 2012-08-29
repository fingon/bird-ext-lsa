/*
 * $Id: elsa_ac.c $
 *
 * Author: Benjamin Paterson <paterson.b@gmail.com>
 *         Markus Stenberg <fingon@iki.fi>
 *
 *
 * Created:       Wed Aug  1 14:26:19 2012 mstenber
 * Last modified: Wed Aug 29 12:58:45 2012 mstenber
 * Edit time:     460 min
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
#include "elsa_ac.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
  ELSA_DEBUG("running %s", cmd);
#ifndef ELSA_UNITTEST_AC_NO_EXEC
  return system(cmd);
#else
  return 0;
#endif /* !ELSA_UNITTEST_AC_NO_EXEC */
}

static bool
configure_add_prefix(elsa e,
                     elsa_if i,
                     elsa_prefix px,
                     u32 rid,
                     int pa_priority)
{
  elsa_ap ap;
  u32 my_rid = elsai_get_rid(e->client);
  const char *ifname;

  /* Get the interface name */
  ifname = elsai_if_get_name(e->client, i);
  if (!ifname)
    {
      ELSA_ERROR("no interface name for interface %p when configuring prefix",
                 i);
      return false;
    }

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
  if (platform_handle_prefix(ap, true) != 0 && e->note_address_add_failures)
    {
      ELSA_ERROR("unable to set up prefix - bailing");
      elsai_free(e->client, ap);
      return false;
    }
  /* Mark that we need to originate new AC LSA at some point */
  e->need_originate_ac = true;

  /* Add to list of active assignments */
  list_add(&ap->list, &e->aps);

  return true;
}

static bool
configure_del_prefix(elsa e, elsa_ap ap)
{
  if (ap->platform_done)
    if (!platform_handle_prefix(ap, false))
      return false;

  /* And from the internal datastructure */
  list_del(&ap->list);
  elsai_free(e->client, ap);

  /* Mark that we need to originate new AC LSA at some point */
  e->need_originate_ac = true;

  return true;
}

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
    *offset += LSA_AC_TLV_SPACE(tlv_size);

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
net_in_net_raw(void *p, int p_len, void *net, int net_len)
{
  if (p_len < net_len)
    return false;
  assert(p_len >= 0 && net_len >= 0 && p_len <= 128 && net_len <= 128);
  /* p's mask has >= net's mask length */
  /* XXX - care about sub-/16 correctness */
  return memcmp(net, p, net_len/8) == 0;
}


static bool
net_in_net(elsa_prefix p, elsa_prefix net)
{
  return net_in_net_raw(p->addr, p->len, net->addr, net->len);
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
assignment_find(elsa e, elsa_if i, elsa_prefix usp)
{
  u32 my_rid = elsai_get_rid(e->client);
  elsa_ap ap;
  const char *ifname = elsai_if_get_name(e->client, i);

  list_for_each_entry(ap, &e->aps, list)
    {
      if(ap->rid == my_rid
         && strcmp(ap->ifname, ifname) == 0
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
random_prefix(elsa e, elsa_prefix px, elsa_prefix pxsub, u32 rid,
              const char *ifname, int i)
{
  elsa_md5 md5;
  char md5sum[16];
  int st;

  md5 = elsai_md5_init(e->client);
  assert(md5);
  elsai_md5_update(md5, ifname, strlen(ifname));
  elsai_md5_update(md5, (char *)&rid, sizeof(rid));
  elsai_md5_update(md5, (char *)&i, sizeof(i));
  elsai_md5_final(md5, md5sum);

  /* FIXME - do correct bitmask operations */
  memcpy(&pxsub->addr, &px->addr, 16);
  st = px->len/16;
  memcpy(&pxsub->addr[st], md5sum, 16-2*st);
  st = pxsub->len/16;
  memset(&pxsub->addr[st], 0, 16-2*st);
}

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
  elsa_lsa lsa;
  struct ospf_lsa_ac_tlv_v_asp *asp;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;

  /* Local state */
  list_for_each_entry(ap, &e->aps, list)
    if (net_in_net(&ap->px, px) || net_in_net(px, &ap->px))
      return true;

  LOOP_ELSA_AC_LSA(e, lsa)
    {
      LOOP_AC_LSA_IFAP_START(lsa, ifap)
        {
          LOOP_IFAP_ASP_START(ifap, asp)
            {
              if (net_in_net_raw(asp->prefix, asp->pxlen,
                                 px->addr, px->len))
                if (elsai_get_rid(e->client) != elsai_lsa_get_rid(lsa))
                  return true;
            }
          LOOP_END;
        }
      LOOP_END;
    }
  return false;
}

/**
 * choose_prefix - Choose a prefix of specified length from
 * a usable prefix and a list of sub-prefixes in use
 * @pxu: The usable prefix
 * @px: A pointer to the prefix structure. Length must be set.
 *
 * This function stores a unused prefix of specified length from
 * the usable prefix @pxu, and returns true,
 * or clears px->addr and returns false if
 * all prefixes are in use.
 *
 * This function will never select the numerically highest /64 prefix
 * in the usable prefix (it is considered reserved).
 */
static bool
choose_prefix(elsa e, elsa_if ei, elsa_prefix pxu, elsa_prefix px)
{
  /* (Stupid) Algorithm:

     - try a random prefix until success or 10 attempts have passed

     - if failure, iterate through the whole usable prefix pxu,
     starting from random position, looking for a prefix that is
     available.
  */
  struct elsa_prefix_struct start_prefix;
  u32 my_rid = elsai_get_rid(e->client);
  int i;

  for (i=0; i<10; i++)
    {
      random_prefix(e, pxu, px, my_rid, elsai_if_get_name(e->client, ei), i);
      if (!in_use(e, px))
        return true;
    }
  start_prefix = *px;

  /* If this precondition is not true, we may wind up in a infinite loop. */
  assert(net_in_net(px, pxu));
  do
    {
      if (!in_use(e, px))
        {
          return true;
        }
      u8 *c = (u8 *)&px->addr;
      for (i = 64/8-1 ; i >= 0 ; i--)
        {
          /* If we roll over to zero, we continue iteration. */
          if (++c[i])
            break;
        }
      /* If we've rolled outside the assigned prefix region, restart
       * at the start. */
      if (!net_in_net(px, pxu))
        {
          memcpy(px->addr, pxu->addr, 16);
        }
    }
  while (memcmp(&start_prefix.addr, &px->addr, 16) != 0);
  return false;

}

static void
asp_extract_elsa_prefix(struct ospf_lsa_ac_tlv_v_asp *asp,
                        elsa_prefix p)
{
  assert(asp);
  assert(p);
  p->len = asp->pxlen;
  if (p->len > 128)
    p->len = 128;
  memset(p->addr, 0, 16);
  memcpy(p->addr, asp->prefix, p->len/8);
}

static void
usp_extract_elsa_prefix(struct ospf_lsa_ac_tlv_v_usp *usp,
                        elsa_prefix p)
{
  assert(sizeof(struct ospf_lsa_ac_tlv_v_asp)
         == sizeof(struct ospf_lsa_ac_tlv_v_usp));
  asp_extract_elsa_prefix((struct ospf_lsa_ac_tlv_v_asp *) usp, p);
}

static bool
have_precedence_over_us(elsa e, elsa_if i,
                        elsa_lsa lsa,
                        struct ospf_lsa_ac_tlv_v_ifap *ifap)
{
  u8 my_priority = elsai_if_get_priority(e->client, i);
  u32 my_rid = elsai_get_rid(e->client);
  u32 other_rid = elsai_lsa_get_rid(lsa);

  if (ifap->pa_priority > my_priority)
    return true;
  if (ifap->pa_priority == my_priority
      && other_rid > my_rid)
    return true;
  return false;
}


/**
 * try_assign_unused - Try to assign an unused prefix of specified @length.
 * @self_r_px: if this is not set to NULL and a successful assignment takes place,
 * removes this prefix (this must be the reserved prefix).
 */
static void
try_assign_unused(elsa e, elsa_if i, elsa_prefix usp)
{
  struct elsa_prefix_struct px;
  u8 my_priority = elsai_if_get_priority(e->client, i);
  u32 my_rid = elsai_get_rid(e->client);

  memset(&px, 0, sizeof(px));
  px.len = 64;

  if (choose_prefix(e, i, usp, &px))
    {
      configure_add_prefix(e, i, &px,
                           my_rid,
                           my_priority);
    }
}

/* Draft 6.3.2(/3) - check conflicts across entire network */
/* XXX - draft says 'it must be depracated', which is not right? The
 * 'it' refers to LSA which has preference!*/
static void
try_depracate_if_conflict(elsa e, elsa_if i,
                          elsa_ap current_ap, elsa_prefix usp)
{
  elsa_lsa lsa;
  u32 my_rid = elsai_get_rid(e->client);
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  struct ospf_lsa_ac_tlv_v_asp *asp;

  /* Must check for conflicts elsewhere. */
  LOOP_ELSA_AC_LSA(e, lsa)
    {
      u32 other_rid = elsai_lsa_get_rid(lsa);

      /* We're considering just other hosts here */
      if (other_rid == my_rid)
        continue;
      LOOP_AC_LSA_IFAP_START(lsa, ifap)
        {
          LOOP_IFAP_ASP_START(ifap, asp)
            {
              /* Check that the prefix matches */
              if (!net_in_net_raw(current_ap->px.addr,
                                  current_ap->px.len,
                                  asp->prefix,
                                  asp->pxlen))
                continue;

              /* It has to have precedence over us. */
              if (!have_precedence_over_us(e, i, lsa, ifap))
                continue;

              /* Woah. We shouldn't be doing this. */
              /* Release the prefix. */
              configure_del_prefix(e, current_ap);
              try_assign_unused(e, i, usp);
              return;
            } LOOP_END;
        } LOOP_END;
    }

  /* No conflicts, we're still king. */
  current_ap->valid = 1;
}

/** ospf_pxassign_usp_ifa - Main prefix assignment algorithm
 *
 * @ifname: The Current Interface
 * @usp: The Current Usable Prefix
 */
static void
pxassign_if_usp(elsa e, elsa_if i, struct ospf_lsa_ac_tlv_v_usp *cusp)
{
  struct elsa_prefix_struct usp, other_assigned_prefix;
  struct ospf_lsa_ac_tlv_v_usp *usp2;
  struct ospf_lsa_ac_tlv_v_asp *asp;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  elsa_ap current_ap = NULL;
  elsa_lsa lsa;
  u32 my_rid = elsai_get_rid(e->client);

  usp_extract_elsa_prefix(cusp, &usp);

  //OSPF_TRACE(D_EVENTS, "Starting prefix assignment algorithm for prefix %I/%d", ip, pxlen);

  /* 6.3.0 (1) - check if prefix contained in larger prefix */
  /* XXX - This sounds .. wrong. Should it not be the inverse for egress
   * filtering reasons? -MSt */
  LOOP_ELSA_AC_LSA(e, lsa)
    LOOP_AC_LSA_USP_START(lsa, usp2)
    {
      if (net_in_net_raw(usp.addr, usp.len, usp2->prefix, usp2->pxlen)
          && usp.len != usp2->pxlen)
        {
          ELSA_DEBUG("prefix covered by other prefix");
          return;
        }
    } LOOP_END;


  /* 6.3.0 (2) */

  /* Let's see if we have assignment for that prefix on the link */
  current_ap = assignment_find(e, i, &usp);
  bool we_assigned = current_ap && current_ap->rid == my_rid;

  /* Let's also see if we have priority, and if someone else has
   * assigned something on the link. We care only about the highest
   * priority router's assignments - the rest, if they exist, can be
   * ignored. */
  bool have_priority = true;
  bool have_assigned_priority = true;
  struct ospf_lsa_ac_tlv_v_asp *other_assigned = NULL;
  u32 other_assigned_rid = 0;
  u32 other_assigned_priority = 0;
  memset(&other_assigned_prefix, 0, sizeof(other_assigned_prefix));

  LOOP_ELSA_AC_LSA(e, lsa)
    {
      u32 other_rid = elsai_lsa_get_rid(lsa);
      /* We're considering just other hosts here */
      if (other_rid == my_rid)
        continue;

      LOOP_AC_LSA_IFAP_START(lsa, ifap)
        {
          /* if (ifap->id == elsai_if_get_neigh_iface_id(e->client, i, other_rid)) */
            {
              bool preferrable = have_precedence_over_us(e, i, lsa, ifap);
              if (preferrable)
                {
                  have_priority = false;
                  other_assigned_rid = other_rid;
                }
              LOOP_IFAP_ASP_START(ifap, asp)
                {
                  if (net_in_net_raw(asp->prefix, asp->pxlen,
                                     usp.addr, usp.len))
                    {
                      other_assigned = asp;
                      if (preferrable)
                        have_assigned_priority = false;
                      asp_extract_elsa_prefix(asp, &other_assigned_prefix);
                    }
                } LOOP_END;
            }
        } LOOP_END;
    }

  /* 6.3.0 subsections follow */
  /* (4.1) */
  /* Local ownership (highest priorityamong assigned). */
  if (we_assigned && have_assigned_priority)
    {
      /* 6.3.2 */
      try_depracate_if_conflict(e, i, current_ap, &usp);
      return;
    }

  /* (4.2) */
  /* There's an assignment on the link, and we don't have priority.
   * Must get rid of local assignment if any, and use the other one.
   */
  if (other_assigned)
    {
      /* 6.3.4 */
      /* TODO - we're making a shortcut here, we don't check for
       * conflciting other assignments on the link. Eventually this
       * will result in consistent state anyway, though. */
      if (current_ap)
        {
          /* We're using it already. Great! */
          /* TODO: Should we check prefix is the same too? */
          if (current_ap->rid == other_assigned_rid)
            {
              current_ap->valid = 1;
              return;
            }

          /* We have assignment from somewhere elsewhere. Let's get
           * rid of it. */
          configure_del_prefix(e, current_ap);
        }

      configure_add_prefix(e, i, &other_assigned_prefix,
                           other_assigned_rid,
                           other_assigned_priority);
      return;
    }

  /* (4.3) */
  /* No assignment, but we have priority - assign (if no duplicates) */
  /* XXX - draft says we should check for other assignments
   * on the link. However, if we have priority, why would we? */
  if (have_priority && !other_assigned &&  !we_assigned)
    {
      try_assign_unused(e, i, &usp);
      return;
    }

  /* (4.4) */
  /* No assignment, but not highest RID on the link either. */
}

/**
 * ospf_pxassign - Run prefix assignment algorithm for
 * usable prefixes advertised by AC LSAs.
 */
static void
pxassign(elsa e)
{
  struct ospf_lsa_ac_tlv_v_usp *usp;
  elsa_ap ap, nap;
  elsa_if i;
  elsa_lsa lsa;
  int decremented = 0;
  int deleted = 0;

  /* Mark all assignments as invalid */
  list_for_each_entry(ap, &e->aps, list)
    {
      if (ap->valid)
        {
          ap->valid--;
          decremented++;
        }
    }
  if (decremented)
    ELSA_DEBUG("decremented validity of %d assigned prefixes", decremented);

  /* Iterate through the (if,usp) pairs */
  LOOP_ELSA_IF(e, i)
    {
      const char *ifname = elsai_if_get_name(e->client, i);

      if (!ifname || !isalnum(ifname[0]))
        continue;

      ELSA_DEBUG("considering interface %s", ifname);

      LOOP_ELSA_AC_LSA(e, lsa)
        LOOP_AC_LSA_USP_START(lsa, usp)
        {
          ELSA_DEBUG(" considering usp %p", usp);
          pxassign_if_usp(e, i, usp);
        } LOOP_END;
    }

  /* Remove all this area's iface's invalid assignments */
  list_for_each_entry_safe(ap, nap, &e->aps, list)
    {
      if(!ap->valid)
        {
          configure_del_prefix(e, ap);
          deleted++;
        }
    }
  if (deleted)
    ELSA_DEBUG("deleted %d prefixes", deleted);
}

/* Wrappers s.t. we can use BIRD-like API .. laziness at work. */
static inline void *lsab_alloc(elsa e, int size)
{
  unsigned char *buf = e->tail;
  unsigned char *ntail = buf + size;

  /* We can't be out of space */
  assert(ntail < (unsigned char *)&e->tail);

  e->tail = ntail;
  return buf;
}

static inline void lsab_put_prefix(elsa e, elsa_prefix p)
{
  uint8_t *buf = lsab_alloc(e, IPV6_PREFIX_SPACE(p->len));

  *buf++ = p->len;
  *buf++ = 0;
  *buf++ = 0;
  *buf++ = 0;
  memcpy(buf, p->addr, (p->len+7)/8);
}

static inline void lsab_set_tlv(struct ospf_lsa_ac_tlv_header *h,
                                short type,
                                short length)
{
  h->type = htons(type);
  h->length = htons(length);
}

/**
 * add_rhwf_tlv - Adds Router-Hardware-Fingerprint TLV to LSA buffer
 * @po: The proto_ospf to which the LSA buffer belongs to
 *
 * This function is called by originate_ac_lsa_body.
 */
static void
add_rhwf_tlv(elsa po)
{
  struct ospf_lsa_ac_tlv_header *rhwf;
  char *buf = "12345678901234567890123456789012";
  int len = strlen(buf);

  /* XXX - get real print from somewhere */
  rhwf = lsab_alloc(po, sizeof(struct ospf_lsa_ac_tlv_header) + len);
  lsab_set_tlv(rhwf, LSA_AC_TLV_T_RHWF, len);
  memcpy(rhwf->value, buf, len);
}

/**
 * add_usp_tlvs - Adds Usable Prefix TLVs to LSA buffer
 * @po: The proto_ospf to which the LSA buffer belongs to
 *
 * This function is called by originate_ac_lsa_body.
 */
static void
add_usp_tlvs(elsa e)
{
  struct ospf_lsa_ac_tlv_header *usp;
  struct elsa_prefix_struct px;

  elsa_ac_usp u;
  void *data;
  int data_bits;

  for (u = elsai_ac_usp_get(e->client);
       u;
       u = elsai_ac_usp_get_next(e->client, u))
    {
      memset(&px, 0, sizeof(px));

      elsai_ac_usp_get_prefix(e->client, u, &data, &data_bits);
      assert(data_bits % 8 == 0);
      assert(data_bits >= 0 && data_bits <= 128);
      px.len = data_bits;
      memcpy(px.addr, data, data_bits / 8);

      usp = lsab_alloc(e, sizeof(struct ospf_lsa_ac_tlv_header));
      lsab_set_tlv(usp, LSA_AC_TLV_T_USP, IPV6_PREFIX_SPACE_NOPAD(px.len));
      lsab_put_prefix(e, &px);
    }
}

/**
 * add_asp_tlvs - Adds Assigned Prefix TLVs to LSA buffer
 * @ifa: Only assigned prefixes for this interface will be advertised.
 *
 * This function is called by originate_ac_lsa_body.
 */
static void
add_asp_tlvs(elsa e, elsa_if ei)
{
  elsa_ap ap;
  struct ospf_lsa_ac_tlv_header *asp;
  const char *ifname = elsai_if_get_name(e->client, ei);

  list_for_each_entry(ap, &e->aps, list)
    {
      /* only advertise prefixes for which we are responsible */
      if (ap->rid != ap->my_rid)
        continue;

      /* if different interface, too bad */
      if (strcmp(ap->ifname, ifname) != 0)
        continue;

      asp = lsab_alloc(e, sizeof(struct ospf_lsa_ac_tlv_header));
      asp->type = LSA_AC_TLV_T_ASP;
      asp->length = IPV6_PREFIX_SPACE_NOPAD(ap->px.len);
      lsab_put_prefix(e, &ap->px);
    }
}

/**
 * add_ifap_tlvs - Adds Interface Prefixes TLVs to LSA buffer
 * @oa: Only assigned prefixes in this area will be advertised.
 *
 * This function is called by originate_ac_lsa_body.
 */
static void
add_ifap_tlvs(elsa e)
{
  elsa_if i;
  struct ospf_lsa_ac_tlv_v_ifap *ifap;
  unsigned char *pos;

  // Add all self-responsible prefixes from asp_lists of all interfaces in area
  LOOP_ELSA_IF(e, i)
    {
      pos = e->tail;
      ifap = lsab_alloc(e, sizeof(struct ospf_lsa_ac_tlv_v_ifap));
      ifap->id = elsai_if_get_index(e->client, i);
      ifap->pa_priority = elsai_if_get_priority(e->client, i);
      ifap->reserved8_1 = 0;
      ifap->reserved8_2 = 0;
      ifap->pa_pxlen = 64;

      add_asp_tlvs(e, i);
      lsab_set_tlv((struct ospf_lsa_ac_tlv_header *)ifap,
                   LSA_AC_TLV_T_IFAP,
                   e->tail - pos - sizeof(struct ospf_lsa_ac_tlv_header));
    }
}

static void
create_ac_lsa_body(elsa e)
{
  e->tail = e->buf;
  add_rhwf_tlv(e);
  add_usp_tlvs(e);
  add_ifap_tlvs(e);
  //*length = po->lsab_used + sizeof(struct ospf_lsa_header);
}

/**
 * originate_ac_lsa - build new instance of Auto Configuration LSA
 * @oa: ospf_area which this LSA is built to
 *
 * This function builds an AC LSA. This function is mostly called from
 * area_disp(). Builds new LSA, increases sequence number (if old
 * instance exists) and sets age of LSA to zero.
 */
void
originate_ac_lsa(elsa e)
{
  create_ac_lsa_body(e);
  /* Output whole buffer as debug.. expensive, but possibly useful. */
  if (elsai_get_log_level() >= ELSA_DEBUG_LEVEL_DEBUG)
    {
      int len = e->tail - e->buf;
      char *buf = elsai_calloc(e->client, len * 2 + 1);
      assert(buf);
      int i;
      for (i = 0 ; i < len ; i++)
        {
          sprintf(buf+2*i, "%02x", e->buf[i]);
        }
      ELSA_DEBUG("new LSA (of %d bytes): %s", len, buf);
      elsai_free(e->client, buf);
    }
  elsai_lsa_originate(e->client,
                      LSA_T_AC, /* type */
                      0, /* id */
                      ++e->ac_sn,
                      e->buf,
                      e->tail - e->buf);
}


void elsa_ac_init(elsa elsa)
{
  INIT_LIST_HEAD(&elsa->aps);
}

void elsa_ac_uninit(elsa e)
{
  elsa_ap ap, nap;

  /* Get rid of the prefixes that are still hanging around */
  list_for_each_entry_safe(ap, nap, &e->aps, list)
    {
      /* If this fails, _too bad_. */
      configure_del_prefix(e, ap);
    }
}

void elsa_ac(elsa e)
{
  ELSA_DEBUG("ac - performing prefix assignment");

  /* Run prefix assignment. */
  pxassign(e);

  /* Originate LSA if necessary. */
  if (e->need_originate_ac)
    {
      e->need_originate_ac = false;
      ELSA_DEBUG("ac - originating new ac lsa");
      originate_ac_lsa(e);
    }
  ELSA_DEBUG("ac - done");
}
