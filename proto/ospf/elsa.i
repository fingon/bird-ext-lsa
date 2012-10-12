%module elsac
%include "typemaps.i"

%{
#include "elsa.h"
#include "elsa_internal.h"
%}

// C input binary buffers
%typemap(in, numinputs=1) (const unsigned char *body, size_t body_len) {
  if(!lua_isstring(L,$input)) SWIG_fail_arg("???",$input,"<lua string>");
  $1 = (unsigned char *)lua_tolstring(L, $input, &$2);
}

// C output binary buffers
%typemap(in, numinputs=0) (unsigned char **body, size_t *body_len) (unsigned char *b, size_t bs) {
  $1 = &b;
  $2 = &bs;
 }

%typemap(argout) (unsigned char **body, size_t *body_len) {
  lua_pushlstring(L, *$1, *$2); SWIG_arg++;
}


 /* Stuff to make it behave sanely */
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

/* from ospf.h */
#define LSA_T_RT	0x2001
#define LSA_T_NET	0x2002
#define LSA_T_SUM_NET	0x2003
#define LSA_T_SUM_RT	0x2004
#define LSA_T_EXT	0x4005
#define LSA_T_NSSA	0x2007
#define LSA_T_LINK	0x0008
#define LSA_T_PREFIX	0x2009

 /* From elsa_platform.h */

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


/* Only externally visible pointer elsa itself provides. */
typedef struct elsa_struct *elsa;

typedef unsigned short elsa_lsatype;

/* AF-wise relevant bits */
struct elsa_struct {
  elsa_client client;

  bool note_address_add_failures;

  bool need_ac;
  bool need_originate_ac;
};

/* From elsa.h */

/* Get current router ID. */
uint32_t elsai_get_rid(elsa_client client);

/* (Try to) change the router ID of the router. */
void elsai_change_rid(elsa_client client);

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

/* Get neighbor's values */
uint32_t elsai_neigh_get_rid(elsa_client client, elsa_neigh neigh);
uint32_t elsai_neigh_get_iid(elsa_client client, elsa_neigh neigh);

/* Get next neighbor (in a list) */
elsa_neigh elsai_neigh_get_next(elsa_client client, elsa_neigh neigh);

/************************************************ Configured AC USP handling */

/* Get first available usable prefix */
elsa_ac_usp elsai_ac_usp_get(elsa_client client);

/* Get next available usable prefix */
elsa_ac_usp elsai_ac_usp_get_next(elsa_client client, elsa_ac_usp usp);

/* Get the prefix's contents. The result_size is the size of result in bits,
 * and result pointer itself points at the prefix data. */
void elsai_ac_usp_get_prefix(elsa_client client, elsa_ac_usp usp,
                             void **result, int *result_size_bits);

/* LUA-specific magic */
elsa elsa_active_get(void);
elsa_lsa elsa_active_lsa_get(void);
void elsa_log_string(const char *string);
