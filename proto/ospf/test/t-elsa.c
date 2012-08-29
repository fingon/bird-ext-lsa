/*
 * $Id: t-elsa.c $
 *
 * Author: Markus Stenberg <fingon@iki.fi>
 *
 * Created:       Tue Aug 28 12:10:30 2012 mstenber
 * Last modified: Wed Aug 29 13:14:01 2012 mstenber
 * Edit time:     121 min
 *
 */

/*
 * This is mock-ified test suite of the ELSA things.
 *
 * It is completely stand-alone, and implements the ELSA platform
 * API. To avoid elsa_platform.h,
 */

#define ELSA_UNITTEST_AC_NO_EXEC

#include <stdio.h>

#define elsai_log(file,line,level,fmt, args...)         \
do {                                                    \
  printf("[debug]%s:%d " fmt "\n", file, line, ##args);      \
 } while(0)

#include "t-elsa.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Include the rest of ELSA .c files (ugh but hey.. if it works..) */
struct elsa_platform_struct {
};
#include "elsa.c"
#include "elsa_ac.c"


#define FAKE_CLIENT 0x12345678

#define RB_SIZE 1234
#define LSAS_SIZE 123
#define IFS_SIZE 12
#define USPS_SIZE 12

/* Static data structures used for storing test data */

int rb[RB_SIZE];
int rb_position;

#define RB_INIT()       \
do {                    \
  rb_position = 0;      \
 } while(0)

#define RB_ADD_INT(x)                   \
do {                                    \
  assert(rb_position < RB_SIZE);        \
  rb[rb_position++] = x;                \
} while(0)

#define RB_ADD_BYTES(data, data_len)                                    \
do {                                                                    \
  int i;                                                                \
  int t;                                                                \
  int left;                                                             \
  unsigned char *c = (unsigned char *)data;                             \
  RB_ADD_INT((data_len + sizeof(int) - 1) / sizeof(int));               \
  for (i = 0 ; i < data_len ; i += sizeof(int))                         \
    {                                                                   \
      t = 0;                                                            \
      left = data_len - i;                                              \
      memcpy(&t, c + i, (left < sizeof(int) ? left : sizeof(int)));     \
      RB_ADD_INT(t);                                                    \
    }                                                                   \
 } while(0)

#define R_ORIGINATE_LSA 42

struct telsa_lsa_struct {
  int i;
  elsa_lsatype type;
  uint32_t rid;
  uint32_t lsid;
  unsigned char body[1024];
  size_t body_len;
};

int free_lsa;
struct telsa_lsa_struct rlsas[LSAS_SIZE];
elsa_lsa lsas[LSAS_SIZE+1];

struct telsa_if_struct {
  int i;
  char name[16];
  uint32_t index;
};

int free_if;
struct telsa_if_struct rifs[IFS_SIZE];
elsa_if ifs[IFS_SIZE+1];

struct telsa_usp_struct {
  int i;
  unsigned char prefix[16];
  int prefix_bits;
};

int free_usp;
struct telsa_usp_struct rusps[USPS_SIZE];
elsa_ac_usp usps[USPS_SIZE+1];

/************************************* Actual dummy implementation of elsa.h */

elsa global_elsa;

void *elsai_calloc(elsa_client client, size_t size)
{
  assert(client == FAKE_CLIENT);
  return calloc(1, size);
}

void elsai_free(elsa_client client, void *ptr)
{
  assert(client == FAKE_CLIENT);
  free(ptr);
}

uint32_t elsai_get_rid(elsa_client client)
{
  assert(client == FAKE_CLIENT);
  return 123;
}

void elsai_change_rid(elsa_client client)
{
  assert(client == FAKE_CLIENT);
  abort();
}

void elsai_lsa_originate(elsa_client client,
                         elsa_lsatype lsatype,
                         uint32_t lsid,
                         uint32_t sn,
                         void *body, size_t body_len)
{
  elsa_lsa lsa = NULL;
  int i;
  bool new_lsa = false;
  bool changed_lsa;

  /* Try to find LSA originated by us in the fake LSDB - if found,
   * overwrite it. */
  for (i = 0 ; i < LSAS_SIZE ; i++)
    if (lsas[i] && lsas[i]->type == lsatype)
      {
        lsa = lsas[i];
        if (elsai_lsa_get_rid(lsa) == elsai_get_rid(client)
            && elsai_lsa_get_lsid(lsa) == lsid)
          break;
        lsa = NULL;
      }
  if (!lsa)
    {
      /* Allocate new LSA */
      lsa = &rlsas[free_lsa++];
      memset(lsa, 0, sizeof(*lsa));
      /* Find first free slot for it in the LSDB */
      for (i = 0 ; i < LSAS_SIZE ; i++)
        if (!lsas[i])
          break;
      assert(i < LSAS_SIZE);
      lsa->i = i;
      lsas[i] = lsa;
      lsa->type = lsatype;
      lsa->lsid = lsid;
      new_lsa = true;
    }
  lsa->rid = elsai_get_rid(client);
  RB_ADD_INT(R_ORIGINATE_LSA);
  RB_ADD_INT(i);
  changed_lsa = (new_lsa || lsa->body_len != body_len || memcmp(lsa->body, body, body_len));
  memset(lsa->body, 0, sizeof(lsa->body));
  memcpy(lsa->body, body, body_len);
  lsa->body_len = body_len;
  ELSA_DEBUG("originate - new:%s - changed:%s",
             new_lsa ? "y": "n",
             changed_lsa ? "y" : "n");
  if (changed_lsa)
    elsa_lsa_changed(global_elsa, lsatype);
}

/* Get first LSA by type. */
elsa_lsa elsai_get_lsa_by_type(elsa_client client, elsa_lsatype lsatype)
{
  int i;

  assert(client == FAKE_CLIENT);
  for (i = 0 ; i < LSAS_SIZE ; i++)
    if (lsas[i] && lsas[i]->type == lsatype)
      return lsas[i];
  return NULL;
}

/* Get next LSA by type. */
elsa_lsa elsai_get_lsa_by_type_next(elsa_client client, elsa_lsa lsa)
{
  int i;

  assert(client == FAKE_CLIENT);
  for (i = lsa->i+1 ; i < LSAS_SIZE ; i++)
    if (lsas[i] && lsas[i]->type == lsa->type)
      return lsas[i];
  return NULL;
}

/* Getters */
elsa_lsatype elsai_lsa_get_type(elsa_lsa lsa)
{
  return lsa->type;
}

uint32_t elsai_lsa_get_rid(elsa_lsa lsa)
{
  return lsa->rid;
}

uint32_t elsai_lsa_get_lsid(elsa_lsa lsa)
{
  return lsa->lsid;
}

void elsai_lsa_get_body(elsa_lsa lsa, unsigned char **body, size_t *body_len)
{
  *body = lsa->body;
  *body_len = lsa->body_len;
}

elsa_if elsai_if_get(elsa_client client)
{
  assert(client == FAKE_CLIENT);
  return ifs[0];
}

elsa_if elsai_if_get_next(elsa_client client, elsa_if ifp)
{
  assert(client == FAKE_CLIENT);
  return ifs[ifp->i+1];
}

const char * elsai_if_get_name(elsa_client client, elsa_if i)
{
  assert(client == FAKE_CLIENT);
  return i->name;
}

uint32_t elsai_if_get_index(elsa_client client, elsa_if i)
{
  assert(client == FAKE_CLIENT);
  return i->index;
}

uint8_t elsai_if_get_priority(elsa_client client, elsa_if i)
{
  assert(client == FAKE_CLIENT);
  return 50;
}

elsa_ac_usp elsai_ac_usp_get(elsa_client client)
{
  assert(client == FAKE_CLIENT);
  return usps[0];
}

elsa_ac_usp elsai_ac_usp_get_next(elsa_client client, elsa_ac_usp usp)
{
  assert(client == FAKE_CLIENT);
  return usps[usp->i+1];
}

void elsai_ac_usp_get_prefix(elsa_client client, elsa_ac_usp usp,
                             void **result, int *result_size_bits)
{
  assert(client == FAKE_CLIENT);
  *result = usp->prefix;
  *result_size_bits = usp->prefix_bits;
}

int elsai_get_log_level(void)
{
  return ELSA_DEBUG_LEVEL_DEBUG;
}

elsa_md5 elsai_md5_init(elsa_client client)
{
  elsa_md5 ctx = elsai_calloc(client, sizeof(*ctx));
  MD5_Init(ctx);
  return ctx;
}

void elsai_md5_update(elsa_md5 md5, const unsigned char *data, int data_len)
{
  MD5_Update(md5, data, data_len);
}

void elsai_md5_final(elsa_md5 md5, void *result)
{
  MD5_Final(result, md5);
  free(md5);
}


/***************************************************** Actual test machinery */

int hex2buf(const char *input, unsigned char *buf, int buf_size)
{
  unsigned char *c = buf;
  int i;
  int bytes = 0;
  char dummy[3];

  while (*input)
    {
      if (bytes == buf_size)
        return -1;
      dummy[0] = *input++;
      dummy[1] = *input++;
      assert(dummy[1]);
      dummy[2] = 0;
      *c++ = strtol(dummy, NULL, 16);
      bytes++;
    }
  return bytes;
}

void reset()
{
  RB_INIT();
  free_lsa = 0;
  memset(lsas, 0, sizeof(lsas));
  free_if = 0;
  memset(ifs, 0, sizeof(ifs));
  free_usp = 0;
  memset(usps, 0, sizeof(usps));
}

/* Minimalist case - no state whatsoever, see that nothing blows up. */
void test_nop()
{
  elsa e;
  reset();
  e = elsa_create(FAKE_CLIENT);
  global_elsa = e;
  elsa_dispatch(e);
  elsa_destroy(e);
  assert(rb_position == 0);
}

/* Test with someone else providing the USP. */
void test_other_origin()
{
  elsa e;
  int i, j;

  const char *base_usp =
    "0001"
    "0020"
    "0000000011111111222222223333333344444444555555556666666677777777"
    "0002"
    "0008"
    "2000000020016442";

  ELSA_DEBUG("!!! test_other_origin");
  reset();
  for (i = 0 ; i < 2 ; i++)
    {
      elsa_if eif = &rifs[i];
      ifs[i] = eif;
      eif->i = i;
      sprintf(eif->name, "eth%d", i);
      eif->index = 42 + i;
    }
  elsa_lsa lsa = &rlsas[0];
  lsas[0] = lsa;
  free_lsa++;
  memset(lsa, 0, sizeof(*lsa));
  lsa->body_len = hex2buf(base_usp, lsa->body, 1024);
  lsa->type = LSA_T_AC;
  e = elsa_create(FAKE_CLIENT);
  global_elsa = e;
  elsa_lsa_changed(e, LSA_T_AC);
  elsa_dispatch(e);
  elsa_dispatch(e);
  elsa_destroy(e);
  assert(rb_position != 0);
}

#define DELEGATED_PREFIX_LENGTH 32

/* Not quite so minimalist test case - provide USP => should allocate
   something, produce TLV. */
void test_own_origin()
{
  elsa e;
  int i, j;

  ELSA_DEBUG("!!! test_own_origin");
  reset();
  for (i = 0 ; i < 2 ; i++)
    {
      elsa_if eif = &rifs[i];
      ifs[i] = eif;
      eif->i = i;
      sprintf(eif->name, "if%d", i);
      eif->index = 42 + i;
    }
  for (i = 0 ; i < 2 ; i++)
    {
      elsa_ac_usp usp = &rusps[i];
      usps[i] = usp;

      usp->i = i;
      /* Two usable prefixes */
      for (j = 0 ; j < DELEGATED_PREFIX_LENGTH/8 ; j++)
        usp->prefix[j] = i + 1;
      usp->prefix_bits = DELEGATED_PREFIX_LENGTH;
    }
  e = elsa_create(FAKE_CLIENT);
  global_elsa = e;
  elsa_dispatch(e);
  elsa_dispatch(e);
  elsa_destroy(e);
  assert(rb_position != 0);
}

void *find_match(const char *input, int type, int index)
{
  /* First off, convert the input to binary. Hope 'buf' is big enough. */
  unsigned char buf[1024];
  int bytes = hex2buf(input, buf, 1024);
  int found = 0;
  void *t = NULL;
  unsigned int offset = 0;

  /* Then, play with find_next_tlv */
  while ((t = find_next_tlv(buf, bytes, &offset, type, NULL, NULL)))
    if (++found == index)
      return t;
  return NULL;
}

void ensure_matches(const char *input, int type, int count)
{
  assert(find_match(input, type, count) != NULL);
  assert(find_match(input, type, count+1) == NULL);
}

void test_parsing()
{
  const char *base_empty_ifaps =
    "0001"
    "0020"
    "0000000011111111222222223333333344444444555555556666666677777777"
    "0004"
    "0008"
    "000000030a000040"
    "0004"
    "0008"
    "000000040a000040";

  const char *base_usp_asps =
    "0001"
    "0020"
    "0000000011111111222222223333333344444444555555556666666677777777"
    "0002"
    "0008"
    "20000000" "20016442"
    "0004"
    "0018"
    "000000030a0000400003000c" "40000000" "2001644215e06017"
    "0004"
    "0018"
    "000000040a0000400003000c" "40000000" "200164428e8f0ff4";

  ensure_matches(base_empty_ifaps, 1, 1);
  ensure_matches(base_empty_ifaps, 4, 2);
  ensure_matches(base_usp_asps, 1, 1);
  ensure_matches(base_usp_asps, 2, 1);
  ensure_matches(base_usp_asps, 4, 2);
}

int main(int argc, char **argv)
{
  test_parsing();
  test_nop();
  test_other_origin();
  test_own_origin();
  return 0;
}
