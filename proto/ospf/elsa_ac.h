/*
 * $Id: elsa_ac.h $
 *
 * Author: Benjamin Paterson <paterson.b@gmail.com>
 *         Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Wed Aug  1 19:06:06 2012 mstenber
 * Last modified: Mon Aug 27 12:19:02 2012 mstenber
 * Edit time:     33 min
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

/* If x is the prefix length in bits, computes the number of
   bytes necessary to represent the prefix (including padding
   to 32-bit multiple length) as:
   length(1 byte) options(1 byte) reserved(2 bytes) prefix(variable) */
#define IPV6_PREFIX_SPACE(x) ((((x) + 63) / 32) * 4)

/* If x is the prefix length in bits, computes the number of
   bytes necessary to represent the prefix, NOT INCLUDING padding */
#define IPV6_PREFIX_SPACE_NOPAD(x) (4 + (((x) + 7) / 8))

/******************************************************************* AC TLVs */

struct ospf_lsa_ac_tlv_header
{
  u16 type;
  u16 length;
  u32 value[];
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
  u8 pa_pxlen;
  u32 rest[]; // Assigned Prefix TLVs
};

/**************************** Benjamin-style macros on top of elsa interface */

static void *
find_next_tlv(void *lsa,
              unsigned int size,
              int *offset,
              u8 type,
              u16 *read_type,
              u16 *read_size);

#define LOOP_ELSA_AC_LSA(e,lsa)                                 \
  for (lsa = elsai_get_lsa_by_type(e->client, LSA_T_AC) ; lsa ; \
       lsa = elsai_get_lsa_by_type_next(e->client, lsa))

#define LOOP_AC_LSA_IFAP_START(lsa,tlv)                                 \
do {                                                                    \
  unsigned int offset = 0;                                              \
  u16 tlv_size;                                                         \
  unsigned char *body;                                                  \
  size_t size;                                                          \
                                                                        \
  elsai_lsa_get_body(lsa, &body, &size);                                \
  while ((tlv = find_next_tlv(body, size, &offset, LSA_AC_TLV_T_IFAP,   \
                              NULL, &tlv_size)))                        \
    {                                                                   \
      if (tlv_size < (sizeof(*tlv)-sizeof(tlv->header)))                \
        break;

#define LOOP_END } } while(0)

#define LOOP_IFAP_ASP_START(ifap,tlv)                           \
do {                                                            \
  unsigned int offset = 0;                                      \
  u16 tlv_size;                                                 \
  int size =                                                    \
    sizeof(struct ospf_lsa_ac_tlv_header)                       \
    + ntohs(ifap->header.length) -                              \
    offsetof(struct ospf_lsa_ac_tlv_v_ifap, rest);              \
                                                                \
  while ((tlv = find_next_tlv(ifap->rest,                       \
                              size,                             \
                              &offset, LSA_AC_TLV_T_ASP,        \
                              NULL, &tlv_size)))                \
    {                                                           \
      if (tlv_size < sizeof(*tlv))                              \
        break;

#define LOOP_AC_LSA_USP_START(lsa,tlv)                                  \
do {                                                                    \
  unsigned int offset = 0;                                              \
  u16 tlv_size;                                                         \
  unsigned char *body;                                                  \
  size_t size;                                                          \
                                                                        \
  elsai_lsa_get_body(lsa, &body, &size);                                \
  while ((tlv = find_next_tlv(body, size, &offset, LSA_AC_TLV_T_USP,    \
                              NULL, &tlv_size)))                        \
    {                                                                   \
      if (tlv_size < (sizeof(*tlv)-sizeof(tlv->header)))                \
        break;

#define LOOP_ELSA_IF(e,ifname)                  \
  for (ifname = elsai_if_get(e->client);        \
       ifname ;                                 \
       ifname=elsai_if_get_next(e->client, ifname))

#endif /* ELSA_AC_H */
