//
// Author: Markus Stenberg <fingon@iki.fi>
//
// Copyright (c) 2012 cisco Systems, Inc.
//

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


// C output nh+ifname

%typemap(in, numinputs=0) (char **output_nh, char **output_if) (char *nh, char *ifname) {
  $1 = &nh;
  $2 = &ifname;
 }


%typemap(argout) (char **output_nh, char **output_if) {
  if (*$1 && *$2)
    {
      lua_pushlstring(L, *$1, strlen(*$1)); SWIG_arg++;
      lua_pushlstring(L, *$2, strlen(*$2)); SWIG_arg++;
    }
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
};

%include "elsa.h"
