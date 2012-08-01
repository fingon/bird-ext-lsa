/*
 * $Id: elsa_ac.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *         Benjamin Paterson <paterson.b@gmail.com>
 *
 * Created:       Wed Aug  1 14:26:19 2012 mstenber
 * Last modified: Wed Aug  1 15:03:07 2012 mstenber
 * Edit time:     16 min
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

/* XXX - put some of this stuff in a platform dependent file. */
static bool
platform_add_prefix(elsa_prefix_node n)
{
  char cmd[128];
  char ip6addr[40];

  /* And then configure it to the system */
  ip_ntop(addr, ip6addr);
  snprintf(cmd, sizeof(cmd), "ip -6 addr add %s%x:%x/%d dev %s",
           ip6addr,
           n->my_rid >> 16,
           n->my_rid & 0xFFFF,
           n->px.len, n->ifname);
  return system(cmd);
}

static bool
configure_ifa_add_prefix(elsa e,
                         elsa_prefix px,
                         u32 rid,
                         u32 my_rid,
                         int pa_priority,
                         const char *ifname)
{
  elsa_prefix_node pxn;

  /* Add the prefix to the interface */
  pxn = elsai_calloc(e->client, sizeof(struct prefix_node));
  if (!pxn)
    return false;

  pxn->px = *px;
  pxn->rid = rid;
  pxn->my_rid = my_rid;
  pxn->pa_priority = pa_priority;
  pxn->valid = 1;
  strncpy(pxn->ifname, ifname, 16);
  
  if (!platform_add_prefix(pxn))
    {
      elsai_free(e->client, pxn);
      return false;
    }
  return true;
}

static int
configure_ifa_del_prefix(struct prefix_node *pxn)
{
  char cmd[128];
  char ip6addr[40];
  int rv;

  /* Remove the prefix from the system */
  // FIXME need a better way to do this.
  // FIXME #2 BIRD seems to create a new ospf_iface struct when addresses change on an interface.
  // Maybe the interfaces' asp_list should be placed elsewhere than in the ospf_iface struct.
  // FIXME #3 This should probably be in a sysdep file.
  ip_ntop(pxn->px.addr, ip6addr);
  snprintf(cmd, sizeof(cmd), "ip -6 addr del %s%x:%x/%d dev %s",
           ip6addr,
           pxn->my_rid >> 16,
           pxn->my_rid & 0xFFFF,
           pxn->px.len, pxn->ifname);
  rv = system(cmd);

  /* And from the internal datastructure */
  rem_node(NODE pxn);
  mb_free(pxn);


  return rv;
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
