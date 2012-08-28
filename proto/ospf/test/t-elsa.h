/*
 * $Id: t-elsa.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Tue Aug 28 12:27:21 2012 mstenber
 * Last modified: Tue Aug 28 14:49:59 2012 mstenber
 * Edit time:     2 min
 *
 */

#ifndef T_ELSA_H
#define T_ELSA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t elsa_client;

/* Opaque LSA blob that doesn't quite have equivalent in BIRD. */
typedef struct telsa_lsa_struct *elsa_lsa;

/* Opaque IF blob. */
typedef struct telsa_if_struct *elsa_if;

/* Opaque USP blob. */
typedef struct telsa_usp_struct *elsa_ac_usp;

typedef char *elsa_md5;

/* Provide fake platform API here */

#define ELSA_PLATFORM_H

#include "elsa.h"

#endif /* T_ELSA_H */
