%module elsa
%{
#include "elsa.h"
%}

 /* Stuff to make it behave sanely */
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;


 /* From elsa_platform.h */

/* BIRD-specific ELSA platform definitions. */
typedef struct proto_ospf *elsa_client;

/* Opaque LSA blob that doesn't quite have equivalent in BIRD. */
typedef struct elsa_lsa_struct *elsa_lsa;

/* Opaque IF blob. */
typedef struct ospf_iface *elsa_if;

/* Opaque USP blob. */
typedef struct elsa_usp_struct *elsa_ac_usp;


/* Only externally visible pointer elsa itself provides. */
typedef struct elsa_struct *elsa;
typedef unsigned short elsa_lsatype;

/* From elsa.h */

/* Get current router ID. */
uint32_t elsai_get_rid(elsa_client client);

/* (Try to) change the router ID of the router. */
// void elsai_change_rid(elsa_client client);

/**************************************************** LSA handling interface */

/* Originate LSA.

   rid is implicitly own router ID.
   age/area is implicitly zero.
*/
void elsai_lsa_originate(elsa_client client,
                         elsa_lsatype lsatype,
                         uint32_t lsid,
                         uint32_t sn,
                         void *body, size_t body_len);

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
/* uint32_t elsai_if_get_neigh_iface_id(elsa_client client, elsa_if i, uint32_t rid); */
uint8_t elsai_if_get_priority(elsa_client client, elsa_if i);

/************************************************ Configured AC USP handling */

/* Get first available usable prefix */
elsa_ac_usp elsai_ac_usp_get(elsa_client client);

/* Get next available usable prefix */
elsa_ac_usp elsai_ac_usp_get_next(elsa_client client, elsa_ac_usp usp);

/* Get the prefix's contents. The result_size is the size of result in bits,
 * and result pointer itself points at the prefix data. */
void elsai_ac_usp_get_prefix(elsa_client client, elsa_ac_usp usp,
                             void **result, int *result_size_bits);

