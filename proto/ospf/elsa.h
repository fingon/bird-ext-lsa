/*
 * $Id: elsa.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 13:31:21 2012 mstenber
 * Last modified: Wed Aug  1 14:01:14 2012 mstenber
 * Edit time:     26 min
 *
 */

/* TODO: - consider the LSA body+data handling semantics - non-copying
 * ones might be nice to have.
 */

#ifndef ELSA_H
#define ELSA_H

/*
 * Public bidirectional interface to the external-LSA code.
 *
 * elsa_* calls are called by the client application.
 *
 * elsai_* calls are called by ELSA code which wants to do something.
 */

/* Whoever includes this file should provide the appropriate
 * definitions for the structures involved. The ELSA code treats them
 * as opaque. */

/* e.g.
   typedef .. my stuff *elsa_client;
   typedef .. *elsa_lsa;
   ( typesafety is mandatory).
 */


/* Only externally visible pointer elsa itself provides. */
typedef struct elsa_struct *elsa;
typedef unsigned short elsa_lsatype;

/******************************************* outside world -> ELSA interface */

/* Create an ELSA instance. NULL is returned in case of an error. */
elsa elsa_create(elsa_client client);

/* Change notifications */
void elsa_lsa_changed(elsa e, elsa_lsatype lsatype);
void elsa_lsa_deleted(elsa e, elsa_lsatype lsatype);

/* Whether ELSA supports given LSAtype. */
bool elsa_supports_lsatype(elsa_lsatype lsatype);

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
u32 elsai_get_rid(elsa_client client);

/* (Try to) change the router ID of the router. */
void elsai_change_rid(elsa_client client);

/* Get first LSA by type. */
elsa_lsa elsai_get_lsa_by_type(elsa_client client, elsa_lsatype lsatype);

/* Get next LSA by type. */
elsa_lsa elsai_get_lsa_by_type_next(elsa_client client, elsa_lsa lsa);

/**************************************************** LSA handling interface */

/* Create LSA.
 *
 * This should create a new elsa_lsa, and return the value to the
 * caller. The body and elsa_data should be COPIED to the lsa. The LSA
 * should remain valid until it's reference count reaches zero via
 * elsai_lsa_decref calls.  Initially created LSA should have
 * reference count of 1.
 */
elsa_lsa elsai_lsa_create(elsa_client client,
                          elsa_lsatype lsatype, u32 rid, u32 lsid,
                          void *body, size_t body_len,
                          bool copy_body,
                          void *elsa_data, size_t elsa_data_len
                          );

/* Getters */
elsa_lsatype elsai_las_get_type(elsa_lsa lsa);
u32 elsai_las_get_rid(elsa_lsa lsa);
u32 elsai_las_get_lsid(elsa_lsa lsa);
u32 elsai_las_get_body(elsa_lsa lsa, unsigned char **body, size_t *body_len);
u32 elsai_las_get_elsa_data(elsa_lsa lsa, unsigned char **data, size_t *data_len);

/* Increment LSA reference count by 1. */
void elsai_lsa_incref(elsa_lsa lsa);

/* Decrement reference count of an LSA.
 *
 * Decrement the reference count of the LSA data structure, and if it
 * is zero, free it.
 */
void elsai_lsa_decref(elsa_lsa lsa);

/* Add LSA. This also implicitly decrements the reference count of the
 * LSA by 1 (as the ownership is moved over to the non-ELSA code). */
void elsai_add_lsa(elsa_client client, elsa_lsa lsa);

/* Delete LSA from LSADB. This also implicitly decrements the
 * reference count of the LSA by 1 (as the ownership is moved over to
 * the non-ELSA code).*/
void elsai_delete_lsa(elsa_client client, elsa_lsa lsa);

#endif /* ELSA_H */
