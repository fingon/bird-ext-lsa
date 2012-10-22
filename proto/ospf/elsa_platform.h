/*
 * $Id: elsa_platform.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:09:11 2012 mstenber
 * Last modified: Mon Oct 22 13:35:25 2012 mstenber
 * Edit time:     17 min
 *
 */

#ifndef ELSA_PLATFORM_H
#define ELSA_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUPPORTED_SIMULTANEOUS_LSA_ITERATIONS 4

/* BIRD-specific ELSA platform definitions. */
typedef struct proto_ospf *elsa_client;

/* Opaque LSA blob that doesn't quite have equivalent in BIRD. */
typedef struct elsa_lsa_struct *elsa_lsa;

/* Opaque IF blob. */
typedef struct ospf_iface *elsa_if;

/* Opaque neighbor blob. */
typedef struct ospf_neighbor *elsa_neigh;

/* Opaque USP blob. */
typedef struct elsa_usp_struct *elsa_ac_usp;

/* MD5 context (dynamically allocated) */
typedef struct MD5Context *elsa_md5;

struct elsa_lsa_struct {
  int hash_bin;
  bool swapped; /* is it swapped to host order? if so, we must reverse it*/
  struct top_hash_entry *hash_entry;
  unsigned char dummy_lsa_buf[65540];
};


struct elsa_platform_struct {
  /* Buffer we use for reversing the pain that is BIRD -
   * the handling of byte-aligned big/little endian stuff differently
   * based on the underlying platform is just herecy.
   *
   * It makes struct definitions for one look rather .. odd.
   */

  /* We support also only X LSA iterations at a time. */
  int last_lsa;

  struct elsa_lsa_struct lsa[SUPPORTED_SIMULTANEOUS_LSA_ITERATIONS];
};

#include "lib/birdlib.h"

#define elsai_log(file,line,level,fmt,...)                      \
do {                                                            \
  /* XXX - care about the 'level'! */                           \
  log(L_TRACE "%s:%d " fmt, file, line, ## __VA_ARGS__);        \
 } while (0)

#undef net_in_net

elsa_lsa elsa_platform_wrap_lsa(elsa_client client,
                                struct top_hash_entry *lsa);

#endif /* ELSA_PLATFORM_H */
