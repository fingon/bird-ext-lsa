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
 * Last modified: Mon Oct  8 14:55:38 2012 mstenber
 * Edit time:     13 min
 *
 */

#ifndef ELSA_INTERNAL_H
#define ELSA_INTERNAL_H

#include <assert.h>
#include <lua.h>

#include "elsa.h"
#include "elsa_linux_list.h"

/* function code 8176(0x1FF0): experimental, U-bit=1, Area Scope */
#define LSA_T_AC        0xBFF0 /* Auto-Configuration LSA */

struct elsa_struct {
  elsa_client client;

  bool note_address_add_failures;

  bool need_ac;
  bool need_originate_ac;

  int ac_sn;
  struct list_head aps;
  struct elsa_platform_struct platform;

  lua_State *l;

  unsigned char buf[65536];
  unsigned char *tail;
};

/* AC code */

typedef struct elsa_prefix_struct {
  /* In network byte order - we can just memcmp from the
   * data we get off the wire. */
  unsigned short addr[8];
  unsigned char len;
} *elsa_prefix;

#define ELSA_IFNAME_LEN 16

typedef struct elsa_ap_struct
{
  /* In-list structure */
  struct list_head list;

  bool platform_done;

  struct elsa_prefix_struct px;

  char ifname[ELSA_IFNAME_LEN]; /* Stored for future use. */
  uint32_t rid;
  uint32_t my_rid;
  bool valid;
} *elsa_ap;

/* AC-specific internal API */
void elsa_ac(elsa elsa);
void elsa_ac_init(elsa elsa);
void elsa_ac_uninit(elsa elsa);

#endif /* ELSA_INTERNAL_H */
