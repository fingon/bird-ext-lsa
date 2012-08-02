/*
 * $Id: elsa_ac.h $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *         Benjamin Paterson <paterson.b@gmail.com>
 *
 * Created:       Wed Aug  1 19:06:06 2012 mstenber
 * Last modified: Thu Aug  2 11:39:19 2012 mstenber
 * Edit time:     15 min
 *
 */

#ifndef ELSA_AC_H
#define ELSA_AC_H

#include <arpa/inet.h>

/*
 * ELSA autoconfigure LSA handling code.
 */

/* Auto-Configuration LSA Type-Length-Value (TLV) types */
#define LSA_AC_TLV_T_RHWF 1/* Router-Hardware-Fingerprint */
#define LSA_AC_TLV_T_USP  2 /* Usable Prefix */
#define LSA_AC_TLV_T_ASP  3 /* Assigned Prefix */
#define LSA_AC_TLV_T_IFAP 4 /* Interface Prefixes */

/* If x is the length of the TLV as specified in its
   Length field, returns the number of bytes used to
   represent the TLV (including type, length + padding) */
#define LSA_AC_TLV_SPACE(x) (4 + (((x + 3) / 4) * 4))

/******************************************************************* AC TLVs */

struct ospf_lsa_ac_tlv_header
{
  u16 type;
  u16 length;
};


struct ospf_lsa_ac_tlv_v_usp /* One Usable Prefix */
{
  struct ospf_lsa_ac_tlv_header header;
  u8 pxlen;
  u8 reserved8;
  u16 reserved16;
  u32 prefix[];
};

struct ospf_lsa_ac_tlv_v_asp /* One Assigned Prefix */
{
  struct ospf_lsa_ac_tlv_header header;
  u8 pxlen;
  u8 reserved8;
  u16 reserved16;
  u32 prefix[];
};

struct ospf_lsa_ac_tlv_v_ifap /* One Interface Prefixes */
{
  struct ospf_lsa_ac_tlv_header header;
  u32 id;
  u8 pa_priority;
  u8 reserved8_1;
  u8 reserved8_2;
  u8 pa_pxlen; // must be PA_PXLEN_D or PA_PXLEN_SUB
  u32 rest[]; // Assigned Prefix TLVs
};

/*************************************************** Iterating functionality */

static void *
find_next_tlv(void *lsa,
              unsigned int size,
              int *offset,
              u8 type,
              u16 *read_type,
              u16 *read_size);

/* Look at the given data entry. Return true if it's desirable to
 * continue iteration.
 */

#define ITERATOR(t,name) bool (*name)(t d, void *context)
#define ITERATOR2(t1,t2,name) bool (*name)(t1 d1, t2 d2, void *context)
#define ITERATOR3(t1,t2,t3,name) bool (*name)(t1 d1, t2 d2, t3 d3, void *context)

/* Base AC LSA iteration */

typedef ITERATOR(elsa_lsa, lsa_iterator);

static bool iterate_ac_lsa(elsa e, lsa_iterator fun, void *context)
{
  elsa_lsa lsa;

  for (lsa = elsai_get_lsa_by_type(e->client, LSA_T_AC) ; lsa ;
       lsa = elsai_get_lsa_by_type_next(e->client, lsa))
    {
      if (!fun(lsa, context))
        return false;
    }
  return true;
}

/* IFAP within AC LSA */
typedef ITERATOR2(elsa_lsa, struct ospf_lsa_ac_tlv_v_ifap *, ifap_iterator);

struct context_ac_ifap_struct {
  ifap_iterator ifap_iterator;
  void *ifap_context;
};

static bool iterator_ac_lsa_ifap(elsa_lsa lsa,
                                 void *context)
{
  struct context_ac_ifap_struct *ctx = context;
  unsigned int offset = 0;
  struct ospf_lsa_ac_tlv_v_ifap *tlv;
  u16 tlv_size;

  unsigned char *body;
  size_t size;

  elsai_las_get_body(lsa, &body, &size);
  while ((tlv = find_next_tlv(body, size, &offset, LSA_AC_TLV_T_IFAP,
                              NULL, &tlv_size)))
    {
      if (tlv_size < (sizeof(*tlv)-sizeof(tlv->header)))
        break;
      ctx->ifap_iterator(lsa, tlv, ctx->ifap_context);
    }
  return true;
}

static bool iterate_ac_lsa_ifap(elsa e, ifap_iterator fun, void *context)
{
  struct context_ac_ifap_struct ctx = { .ifap_iterator = fun,
                                        .ifap_context = context};
  return iterate_ac_lsa(e, iterator_ac_lsa_ifap, &ctx);
}

/* ASP within IFAP within AC LSA (...) */
typedef ITERATOR3(elsa_lsa,
                  struct ospf_lsa_ac_tlv_v_ifap *,
                  struct ospf_lsa_ac_tlv_v_asp *,
                  asp_iterator);

struct context_ac_ifap_asp_struct {
  asp_iterator asp_iterator;
  void *asp_context;
};

static bool iterator_ac_lsa_ifap_asp(elsa_lsa lsa,
                                     struct ospf_lsa_ac_tlv_v_ifap *ifap,
                                     void *context)
{
  struct context_ac_ifap_asp_struct *ctx = context;
  unsigned int offset = 0;
  struct ospf_lsa_ac_tlv_v_asp *tlv;
  u16 tlv_size;
  int size =
    sizeof(struct ospf_lsa_ac_tlv_header) + ntohs(ifap->header.length) -
    offsetof(struct ospf_lsa_ac_tlv_v_ifap, rest);
  while ((tlv = find_next_tlv(ifap->rest,
                              size,
                              &offset, LSA_AC_TLV_T_ASP,
                              NULL, &tlv_size)))
    {
      if (tlv_size < sizeof(*tlv))
        break;
      ctx->asp_iterator(lsa, ifap, tlv, ctx->asp_context);
    }
  return true;
}

static bool iterate_ac_lsa_ifap_asp(elsa e, asp_iterator fun, void *context)
{
  struct context_ac_ifap_asp_struct ctx = { .asp_iterator = fun,
                                            .asp_context = context};
  return iterate_ac_lsa_ifap(e, iterator_ac_lsa_ifap_asp, &ctx);
}

/* USP within AC LSA */
typedef ITERATOR2(elsa_lsa, struct ospf_lsa_ac_tlv_v_usp *, usp_iterator);

struct context_ac_usp_struct {
  usp_iterator usp_iterator;
  void *usp_context;
};

static bool iterator_ac_lsa_usp(elsa_lsa lsa,
                                 void *context)
{
  struct context_ac_usp_struct *ctx = context;
  unsigned int offset = 0;
  struct ospf_lsa_ac_tlv_v_usp *tlv;
  u16 tlv_size;

  unsigned char *body;
  size_t size;

  elsai_las_get_body(lsa, &body, &size);
  while ((tlv = find_next_tlv(body, size, &offset, LSA_AC_TLV_T_USP,
                              NULL, &tlv_size)))
    {
      if (tlv_size < (sizeof(*tlv)-sizeof(tlv->header)))
        break;
      ctx->usp_iterator(lsa, tlv, ctx->usp_context);
    }
  return true;
}

static bool iterate_ac_lsa_usp(elsa e, usp_iterator fun, void *context)
{
  struct context_ac_usp_struct ctx = { .usp_iterator = fun,
                                        .usp_context = context};
  return iterate_ac_lsa(e, iterator_ac_lsa_usp, &ctx);
}

/********************************************************* Benjamin's macros */



#endif /* ELSA_AC_H */
