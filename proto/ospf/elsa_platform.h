/*
 * $Id: elsa_platform.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 14:09:11 2012 mstenber
 * Last modified: Wed Aug  1 14:13:22 2012 mstenber
 * Edit time:     2 min
 *
 */

#ifndef ELSA_PLATFORM_H
#define ELSA_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* BIRD-specific ELSA platform definitions. */
typedef struct proto_ospf *elsa_client;

/* Opaque LSA blob that doesn't quite have equivalent in BIRD. */
typedef struct elsa_lsa_struct *elsa_lsa;


#endif /* ELSA_PLATFORM_H */
