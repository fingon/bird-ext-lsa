/*
 * $Id: elsa_internal.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *         Benjamin Paterson <paterson.b@gmail.com>
 *
 *  Copyright (c) 2012 Markus Stenberg
 *       All rights reserved
 *
 * Created:       Wed Aug  1 14:23:23 2012 mstenber
 * Last modified: Wed Aug  1 17:50:41 2012 mstenber
 * Edit time:     8 min
 *
 */

#ifndef ELSA_INTERNAL_H
#define ELSA_INTERNAL_H

#include <assert.h>

#include "elsa.h"
#include "elsa_linux_list.h"

/* function code 8176(0x1FF0): experimental, U-bit=1, Area Scope */
#define LSA_T_AC        0xBFF0 /* Auto-Configuration LSA */

struct elsa_struct {
  elsa_client client;
  bool need_ac;
  struct list_head aps;
};

/* AC code */

typedef struct elsa_prefix_struct {
  /* In network byte order - we can just memcmp from the
   * data we get off the wire. */
  unsigned short addr[8];
  unsigned char len;
} *elsa_prefix;

#define ELSA_IFNAME_LEN 16

/* XXX: Backward compatibility with old code - get rid of these! */
typedef uint32_t u32;
typedef unsigned short u16;
typedef unsigned char u8;

typedef struct elsa_ap_struct
{
  /* In-list structure */
  struct list_head list;

  struct elsa_prefix_struct px;
  
  char ifname[ELSA_IFNAME_LEN]; /* Stored for future use. */
  u32 rid;                      /* Who is responsible for this prefix.
                                   Only relevant for assigned prefixes. */
  u32 my_rid;                   /* My router ID used when configuring
                                   address. (Relevant as it may change.)*/
  int valid;                    /* Used in prefix assignment algorithm.
                                   Only relevant for assigned prefixes. */
#define OSPF_USP_T_MANUAL 1
#define OSPF_USP_T_DHCPV6 2
  u8 type;                      /* Where we learned the prefix from.
                                   Only relevant for usable prefixes. */
  u8 pa_priority;               /* The prefix assignment priority of
                                   the router responsible for this prefix.
                                   Only relevant for assigned prefixes. */
} *elsa_ap;

/* AC-specific internal API */
void elsa_ac(elsa elsa);
void elsa_ac_init(elsa elsa);
void elsa_ac_uninit(elsa elsa);

#endif /* ELSA_INTERNAL_H */
