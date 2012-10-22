/*
 * $Id: elsa.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 13:31:21 2012 mstenber
 * Last modified: Mon Oct 22 14:33:30 2012 mstenber
 * Edit time:     86 min
 *
 */

/* TODO:
 *
 * - consider the LSA body+data handling semantics - non-copying
 * ones might be nice to have.
 */

#ifndef ELSA_H
#define ELSA_H

/*
 * Public bidirectional interface to the external-LSA code.
 *
 * elsa_* calls are called by the client application.
 *
 * elsai_* calls are called by ELSA code which wants the platform code
 * to do something.
 *
 * General design criteria is that the data in network format should
 * be usable directly; however, individual VALUES we move across
 * should be in host order.
 * - LSA payload is in network order
 * - individual LSA fields (e.g.) that are passed through API are in host order
 */

/* Whoever includes this file should provide the appropriate
 * definitions for the structures involved. The ELSA code treats them
 * as opaque. */
#include "elsa_platform.h"
/* e.g.
   typedef .. my stuff *elsa_client;
   typedef .. *elsa_lsa;
   typedef .. *elsa_if;
   typedef .. *elsa_ac_usp;
   ( typesafety is mandatory).
 */


/* Only externally visible pointer elsa itself provides. */
typedef struct elsa_struct *elsa;
typedef unsigned short elsa_lsatype;

/******************************************* outside world -> ELSA interface */

/* Create an ELSA instance. NULL is returned in case of an error. */
elsa elsa_create(elsa_client client);

/* Change notifications */
void elsa_notify_changed_lsa(elsa e, elsa_lsa lsa);
void elsa_notify_deleting_lsa(elsa e, elsa_lsa lsa);

/* Notify ELSA when duplicate LSA has been received. */
void elsa_notify_duplicate_lsa(elsa e, elsa_lsa lsa);

/* Dispatch ELSA action - should be called once a second (or so). */
void elsa_dispatch(elsa e);

/* Destroy an ELSA instance. */
void elsa_destroy(elsa e);

/******************************************* ELSA -> outside world interface */

/* Allocate a block of memory. The memory should be zeroed. */
void *elsai_calloc(elsa_client client, size_t size);

/* Free a block of memory. */
void elsai_free(elsa_client client, void *ptr);


/* Get current router ID. */
uint32_t elsai_get_rid(elsa_client client);

/* (Try to) change the router ID of the router. */
void elsai_change_rid(elsa_client client);

/* Get route to the rid; returned next-hop address + if (NULL if no
   route). */
void elsai_route_to_rid(elsa_client client, uint32_t rid,
                        char **output_nh, char **output_if);

/**************************************************** LSA handling interface */

/* Originate LSA.

   rid is implicitly own router ID.
   age/area is implicitly zero.
*/
void elsai_lsa_originate(elsa_client client,
                         elsa_lsatype lsatype,
                         uint32_t lsid,
                         uint32_t sn,
                         const unsigned char *body, size_t body_len);

/* Get first LSA by type. */
elsa_lsa elsai_get_lsa_by_type(elsa_client client, elsa_lsatype lsatype);

/* Get next LSA by type. */
elsa_lsa elsai_get_lsa_by_type_next(elsa_client client, elsa_lsa lsa);

/* Getters */
elsa_lsatype elsai_lsa_get_type(elsa_lsa lsa);
uint32_t elsai_lsa_get_rid(elsa_lsa lsa);
uint32_t elsai_lsa_get_lsid(elsa_lsa lsa);
void elsai_lsa_get_body(elsa_lsa lsa, unsigned char **body, size_t *body_len);

/******************************************************** Interface handling */

/* Get interface */
elsa_if elsai_if_get(elsa_client client);

/* Get next interface */
elsa_if elsai_if_get_next(elsa_client client, elsa_if ifp);

/* Assorted getters */

const char * elsai_if_get_name(elsa_client client, elsa_if i);
uint32_t elsai_if_get_index(elsa_client client, elsa_if i);
uint8_t elsai_if_get_priority(elsa_client client, elsa_if i);

/* Get first neighbor */
elsa_neigh elsai_if_get_neigh(elsa_client client, elsa_if i);

/********************************************************* Neighbor handling */
uint32_t elsai_neigh_get_rid(elsa_client client, elsa_neigh neigh);
uint32_t elsai_neigh_get_iid(elsa_client client, elsa_neigh neigh);

/* Get next neighbor (in list) */
elsa_neigh elsai_neigh_get_next(elsa_client client, elsa_neigh neigh);

/****************************************************** Debugging / tracing  */

#define ELSA_DEBUG_LEVEL_ERROR 1
#define ELSA_DEBUG_LEVEL_INFO 2
#define ELSA_DEBUG_LEVEL_DEBUG 3

/* Get the debug level */
int elsai_get_log_level(void);
/* void elsai_log(const char *file, int line, int level, const char *fmt, ...); */

#define ELSA_LOG(l,fmt...)                      \
do {                                            \
  if (elsai_get_log_level() >= l) {             \
    elsai_log(__FILE__, __LINE__, l, ##fmt);    \
  }                                             \
 } while(0)

#define ELSA_ERROR(fmt...) ELSA_LOG(ELSA_DEBUG_LEVEL_ERROR, ##fmt)
#define ELSA_INFO(fmt...) ELSA_LOG(ELSA_DEBUG_LEVEL_INFO, ##fmt)
#define ELSA_DEBUG(fmt...) ELSA_LOG(ELSA_DEBUG_LEVEL_DEBUG, ##fmt)

/************************************************************ 'Other stuff'  */

/* LUA cruft */
elsa elsa_active_get(void);
elsa_lsa elsa_active_lsa_get(void);
void elsa_log_string(const char *string);

#endif /* ELSA_H */
