/*
 *	BIRD -- BGP Attributes
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"
#include "lib/resource.h"
#include "lib/string.h"
#include "lib/unaligned.h"

#include "bgp.h"

static int bgp_mandatory_attrs[] = { BA_ORIGIN, BA_AS_PATH, BA_NEXT_HOP };

struct bgp_bucket {
  struct bgp_bucket *next;
  unsigned hash;
  ea_list eattrs[0];
};

struct attr_desc {
  char *name;				/* FIXME: Use the same names as in filters */
  int expected_length;
  int expected_flags;
  int type;
  int (*validate)(struct bgp_proto *p, byte *attr, int len);
  void (*format)(eattr *ea, byte *buf);
};

extern struct attr_desc bgp_attr_table[];

static void
bgp_normalize_set(u32 *dest, u32 *src, unsigned cnt)
{
  memcpy(dest, src, sizeof(u32) * cnt);
  /* FIXME */
}

static void
bgp_rehash_buckets(struct bgp_proto *p)
{
  struct bgp_bucket **old = p->bucket_table;
  struct bgp_bucket **new;
  unsigned oldn = p->hash_size;
  unsigned i, e, mask;
  struct bgp_bucket *b;

  p->hash_size = p->hash_limit;
  DBG("BGP: Rehashing bucket table from %d to %d\n", oldn, p->hash_size);
  p->hash_limit *= 4;
  if (p->hash_limit >= 65536)
    p->hash_limit = ~0;
  new = p->bucket_table = mb_allocz(p->p.pool, p->hash_size * sizeof(struct bgp_bucket *));
  mask = p->hash_size - 1;
  for (i=0; i<oldn; i++)
    while (b = old[i])
      {
	old[i] = b->next;
	e = b->hash & mask;
	b->next = new[e];
	new[e] = b;
      }
  mb_free(old);
}

static struct bgp_bucket *
bgp_new_bucket(struct bgp_proto *p, ea_list *new, unsigned hash)
{
  struct bgp_bucket *b;
  unsigned ea_size = sizeof(ea_list) + new->count * sizeof(eattr);
  unsigned ea_size_aligned = ALIGN(ea_size, CPU_STRUCT_ALIGN);
  unsigned size = sizeof(struct bgp_bucket) + ea_size;
  unsigned i;
  byte *dest;
  unsigned index = hash & (p->hash_size - 1);

  /* Gather total size of non-inline attributes */
  for (i=0; i<new->count; i++)
    {
      eattr *a = &new->attrs[i];
      if (!(a->type & EAF_EMBEDDED))
	size += ALIGN(sizeof(struct adata) + a->u.ptr->length, CPU_STRUCT_ALIGN);
    }

  /* Create the bucket and hash it */
  b = mb_alloc(p->p.pool, size);
  b->next = p->bucket_table[index];
  p->bucket_table[index] = b;
  b->hash = hash;
  memcpy(b->eattrs, new, ea_size);
  dest = ((byte *)b->eattrs) + ea_size_aligned;

  /* Copy values of non-inline attributes */
  for (i=0; i<new->count; i++)
    {
      eattr *a = &new->attrs[i];
      if (!(a->type & EAF_EMBEDDED))
	{
	  struct adata *oa = a->u.ptr;
	  struct adata *na = (struct adata *) dest;
	  memcpy(na, oa, sizeof(struct adata) + oa->length);
	  a->u.ptr = na;
	  dest += ALIGN(na->length, CPU_STRUCT_ALIGN);
	}
    }

  /* If needed, rehash */
  p->hash_count++;
  if (p->hash_count > p->hash_limit)
    bgp_rehash_buckets(p);

  return b;
}

static struct bgp_bucket *
bgp_get_bucket(struct bgp_proto *p, ea_list *old, ea_list *tmp)
{
  ea_list *t, *new;
  unsigned i, cnt;
  eattr *a, *d;
  u32 seen = 0;
  unsigned hash;
  struct bgp_bucket *b;

  /* Merge the attribute lists */
  for(t=tmp; t->next; t=t->next)
    ;
  t->next = old;
  new = alloca(ea_scan(tmp));
  ea_merge(tmp, new);
  t->next = NULL;
  ea_sort(tmp);

  /* Normalize attributes */
  d = new->attrs;
  cnt = new->count;
  new->count = 0;
  for(i=0; i<cnt; i++)
    {
      a = &new->attrs[i];
#ifdef LOCAL_DEBUG
      {
	byte buf[256];
	ea_format(a, buf);
	DBG("\t%s\n", buf);
      }
#endif
      if (EA_PROTO(a->id) != EAP_BGP)
	continue;
      if (EA_ID(a->id) < 32)
	seen |= 1 << EA_ID(a->id);
      *d = *a;
      switch (d->type & EAF_TYPE_MASK)
	{
	case EAF_TYPE_INT_SET:
	  {
	    struct adata *z = alloca(sizeof(struct adata) + d->u.ptr->length);
	    z->length = d->u.ptr->length;
	    bgp_normalize_set((u32 *) z->data, (u32 *) d->u.ptr->data, z->length / 4);
	    d->u.ptr = z;
	    break;
	  }
	default:
	}
      d++;
      new->count++;
    }

  /* Hash */
  hash = ea_hash(new);
  for(b=p->bucket_table[hash & (p->hash_size - 1)]; b; b=b->next)
    if (b->hash == hash && ea_same(b->eattrs, new))
      {
	DBG("Found bucket.\n");
	return b;
      }

  /* Ensure that there are all mandatory attributes */
  for(i=0; i<ARRAY_SIZE(bgp_mandatory_attrs); i++)
    if (!(seen & (1 << bgp_mandatory_attrs[i])))
      {
	log(L_ERR "%s: Mandatory attribute %s missing", p->p.name, bgp_attr_table[bgp_mandatory_attrs[i]].name);
	return NULL;
      }

  /* Create new bucket */
  DBG("Creating bucket.\n");
  return bgp_new_bucket(p, new, hash);
}

void
bgp_rt_notify(struct proto *P, net *n, rte *new, rte *old, ea_list *tmpa)
{
  struct bgp_proto *p = (struct bgp_proto *) P;

  DBG("BGP: Got route %I/%d\n", n->n.prefix, n->n.pxlen);

  if (new)
    {
      struct bgp_bucket *buck = bgp_get_bucket(p, new->attrs->eattrs, tmpa);
      if (!buck)			/* Inconsistent attribute list */
	return;
    }

  /* FIXME: Normalize attributes */
  /* FIXME: Check next hop */
}

static int
bgp_create_attrs(struct bgp_proto *p, rte *e, ea_list **attrs, struct linpool *pool)
{
  ea_list *ea = lp_alloc(pool, sizeof(ea_list) + 3*sizeof(eattr));
  eattr *a = ea->attrs;
  rta *rta = e->attrs;

  ea->next = *attrs;
  *attrs = ea;
  ea->flags = EALF_SORTED;
  ea->count = 3;

  a->id = EA_CODE(EAP_BGP, BA_ORIGIN);
  a->flags = BAF_TRANSITIVE;
  a->type = EAF_TYPE_INT;
  if (rta->source == RTS_RIP_EXT || rta->source == RTS_OSPF_EXT)
    a->u.data = 2;			/* Incomplete */
  else
    a->u.data = 0;			/* IGP */
  a++;

  a->id = EA_CODE(EAP_BGP, BA_AS_PATH);
  a->flags = BAF_TRANSITIVE;
  a->type = EAF_TYPE_AS_PATH;
  if (p->is_internal)
    {
      a->u.ptr = lp_alloc(pool, sizeof(struct adata));
      a->u.ptr->length = 0;
    }
  else
    {
      byte *z;
      a->u.ptr = lp_alloc(pool, sizeof(struct adata) + 4);
      a->u.ptr->length = 4;
      z = a->u.ptr->data;
      z[0] = 2;				/* AS_SEQUENCE */
      z[1] = 1;				/* 1 AS */
      put_u16(z+2, p->local_as);
    }
  a++;

  a->id = EA_CODE(EAP_BGP, BA_NEXT_HOP);
  a->flags = BAF_TRANSITIVE;
  a->type = EAF_TYPE_IP_ADDRESS;
  a->u.ptr = lp_alloc(pool, sizeof(struct adata) + sizeof(ip_addr));
  a->u.ptr->length = sizeof(ip_addr);
  if (p->cf->next_hop_self ||
      !p->is_internal ||
      rta->dest != RTD_ROUTER)
    *(ip_addr *)a->u.ptr->data = p->local_addr;
  else
    *(ip_addr *)a->u.ptr->data = e->attrs->gw;

  return 0;				/* Leave decision to the filters */
}

ea_list *
bgp_path_prepend(struct linpool *pool, eattr *a, ea_list *old, int as)
{
  struct ea_list *e = lp_alloc(pool, sizeof(ea_list) + sizeof(eattr));
  struct adata *olda = a->u.ptr;
  struct adata *newa;

  e->next = old;
  e->flags = EALF_SORTED;
  e->count = 1;
  e->attrs[0].id = EA_CODE(EAP_BGP, BA_AS_PATH);
  e->attrs[0].flags = BAF_TRANSITIVE;
  e->attrs[0].type = EAF_TYPE_AS_PATH;
  if (olda->length && olda->data[0] == 2 && olda->data[1] < 255) /* Starting with sequence => just prepend the AS number */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 2);
      newa->length = olda->length + 2;
      newa->data[0] = 2;
      newa->data[1] = olda->data[1] + 1;
      memcpy(newa->data+4, olda->data+2, olda->length-2);
    }
  else					/* Create new path segment */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 4);
      newa->length = olda->length + 4;
      newa->data[0] = 2;
      newa->data[1] = 1;
      memcpy(newa->data+4, olda->data, olda->length);
    }
  put_u16(newa->data+2, as);
  e->attrs[0].u.ptr = newa;
  return e;
}

static int
bgp_update_attrs(struct bgp_proto *p, rte *e, ea_list **attrs, struct linpool *pool)
{
  eattr *a;

  if (!p->is_internal)
    *attrs = bgp_path_prepend(pool, ea_find(e->attrs->eattrs, EA_CODE(EAP_BGP, BA_AS_PATH)), *attrs, p->local_as);

  a = ea_find(e->attrs->eattrs, EA_CODE(EAP_BGP, BA_NEXT_HOP));
  if (a && (p->is_internal || (!p->is_internal && e->attrs->iface == p->neigh->iface)))
    {
      /* Leave the original next hop attribute, will check later where does it point */
    }
  else
    {
      /* Need to create new one */
      ea_list *ea = lp_alloc(pool, sizeof(ea_list) + sizeof(eattr));
      ea->next = *attrs;
      *attrs = ea;
      ea->flags = EALF_SORTED;
      ea->count = 1;
      a = ea->attrs;
      a->id = EA_CODE(EAP_BGP, BA_NEXT_HOP);
      a->flags = BAF_TRANSITIVE;
      a->type = EAF_TYPE_IP_ADDRESS;
      a->u.ptr = lp_alloc(pool, sizeof(struct adata) + sizeof(ip_addr));
      a->u.ptr->length = sizeof(ip_addr);
      *(ip_addr *)a->u.ptr->data = p->local_addr;
    }

  return 0;				/* Leave decision to the filters */
}

int
bgp_import_control(struct proto *P, rte **new, ea_list **attrs, struct linpool *pool)
{
  rte *e = *new;
  struct bgp_proto *p = (struct bgp_proto *) P;
  struct bgp_proto *new_bgp = (e->attrs->proto->proto == &proto_bgp) ? (struct bgp_proto *) e->attrs->proto : NULL;

  if (p == new_bgp)			/* Poison reverse updates */
    return -1;
  if (new_bgp)
    {
      if (p->local_as == new_bgp->local_as && p->is_internal && new_bgp->is_internal)
	return -1;			/* Don't redistribute internal routes with IBGP */
      return bgp_update_attrs(p, e, attrs, pool);
    }
  else
    return bgp_create_attrs(p, e, attrs, pool);
}

int
bgp_rte_better(rte *new, rte *old)
{
  struct bgp_proto *new_bgp = (struct bgp_proto *) new->attrs->proto;
  struct bgp_proto *old_bgp = (struct bgp_proto *) old->attrs->proto;
  eattr *new_lpref = ea_find(new->attrs->eattrs, EA_CODE(EAP_BGP, BA_LOCAL_PREF));
  eattr *old_lpref = ea_find(old->attrs->eattrs, EA_CODE(EAP_BGP, BA_LOCAL_PREF));

  /* Start with local preferences */
  if (new_lpref && old_lpref)		/* Somebody might have undefined them */
    {
      if (new_lpref->u.data > old_lpref->u.data)
	return 1;
      if (new_lpref->u.data < old_lpref->u.data)
	return 0;
    }

  /* A tie breaking procedure according to RFC 1771, section 9.1.2.1 */
  /* FIXME: Look at MULTI_EXIT_DISC, take the lowest */
  /* We don't have interior distances */
  /* We prefer external peers */
  if (new_bgp->is_internal > old_bgp->is_internal)
    return 0;
  if (new_bgp->is_internal < old_bgp->is_internal)
    return 1;
  /* Finally we compare BGP identifiers */
  return (new_bgp->remote_id < old_bgp->remote_id);
}

static int
bgp_local_pref(struct bgp_proto *p, rta *a)
{
  return 0;				/* FIXME (should be compatible with Cisco defaults?) */
}

static int
bgp_path_loopy(struct bgp_proto *p, eattr *a)
{
  byte *path = a->u.ptr->data;
  int len = a->u.ptr->length;
  int i, n;

  while (len > 0)
    {
      n = path[1];
      len -= 2 - 2*n;
      path += 2;
      for(i=0; i<n; i++)
	{
	  if (get_u16(path) == p->local_as)
	    return 1;
	  path += 2;
	}
    }
  return 0;
}

static int
bgp_check_origin(struct bgp_proto *p, byte *a, int len)
{
  if (len > 2)
    return 6;
  return 0;
}

static void
bgp_format_origin(eattr *a, byte *buf)
{
  static char *bgp_origin_names[] = { "IGP", "EGP", "Incomplete" };

  bsprintf(buf, bgp_origin_names[a->u.data]);
}

static int
bgp_check_path(struct bgp_proto *p, byte *a, int len)
{
  while (len)
    {
      DBG("Path segment %02x %02x\n", a[0], a[1]);
      if (len < 2 ||
	  a[0] != BGP_PATH_AS_SET && a[0] != BGP_PATH_AS_SEQUENCE ||
	  2*a[1] + 2 > len)
	return 11;
      len -= 2*a[1] + 2;
      a += 2*a[1] + 2;
    }
  return 0;
}

static int
bgp_check_next_hop(struct bgp_proto *p, byte *a, int len)
{
  ip_addr addr;

  memcpy(&addr, a, len);
  if (ipa_classify(ipa_ntoh(addr)) & IADDR_HOST)
    return 0;
  else
    return 8;
}

static int
bgp_check_local_pref(struct bgp_proto *p, byte *a, int len)
{
  if (!p->is_internal)			/* Ignore local preference from EBGP connections */
    return -1;
  return 0;
}

static struct attr_desc bgp_attr_table[] = {
  { NULL, -1, 0, 0,						/* Undefined */
    NULL, NULL },
  { "origin", 1, BAF_TRANSITIVE, EAF_TYPE_INT,			/* BA_ORIGIN */
    bgp_check_origin, bgp_format_origin },
  { "as_path", -1, BAF_TRANSITIVE, EAF_TYPE_AS_PATH,		/* BA_AS_PATH */
    bgp_check_path, NULL },
  { "next_hop", 4, BAF_TRANSITIVE, EAF_TYPE_IP_ADDRESS,		/* BA_NEXT_HOP */
    bgp_check_next_hop, NULL },
  { "MED", 4, BAF_OPTIONAL, EAF_TYPE_INT,			/* BA_MULTI_EXIT_DISC */
    NULL, NULL },
  { "local_pref", 4, BAF_OPTIONAL, EAF_TYPE_INT,		/* BA_LOCAL_PREF */
    bgp_check_local_pref, NULL },
  { "atomic_aggr", 0, BAF_OPTIONAL, EAF_TYPE_OPAQUE,		/* BA_ATOMIC_AGGR */
    NULL, NULL },
  { "aggregator", 6, BAF_OPTIONAL, EAF_TYPE_OPAQUE,		/* BA_AGGREGATOR */
    NULL, NULL },
#if 0
  /* FIXME: Handle community lists and remember to convert their endianity and normalize them */
  { 0, 0 },									/* BA_COMMUNITY */
  { 0, 0 },									/* BA_ORIGINATOR_ID */
  { 0, 0 },									/* BA_CLUSTER_LIST */
#endif
};

struct rta *
bgp_decode_attrs(struct bgp_conn *conn, byte *attr, unsigned int len, struct linpool *pool)
{
  struct bgp_proto *bgp = conn->bgp;
  rta *a = lp_alloc(pool, sizeof(struct rta));
  unsigned int flags, code, l, i, type;
  int errcode;
  byte *z, *attr_start;
  byte seen[256/8];
  eattr *e;
  ea_list *ea;
  struct adata *ad;
  neighbor *neigh;
  ip_addr nexthop;

  a->proto = &bgp->p;
  a->source = RTS_BGP;
  a->scope = SCOPE_UNIVERSE;
  a->cast = RTC_UNICAST;
  a->dest = RTD_ROUTER;
  a->flags = 0;
  a->aflags = 0;
  a->from = bgp->cf->remote_ip;
  a->eattrs = NULL;

  /* Parse the attributes */
  bzero(seen, sizeof(seen));
  DBG("BGP: Parsing attributes\n");
  while (len)
    {
      if (len < 2)
	goto malformed;
      attr_start = attr;
      flags = *attr++;
      code = *attr++;
      len -= 2;
      if (flags & BAF_EXT_LEN)
	{
	  if (len < 2)
	    goto malformed;
	  l = get_u16(attr);
	  attr += 2;
	  len -= 2;
	}
      else
	{
	  if (len < 1)
	    goto malformed;
	  l = *attr++;
	  len--;
	}
      if (l > len)
	goto malformed;
      len -= l;
      z = attr;
      attr += l;
      DBG("Attr %02x %02x %d\n", code, flags, l);
      if (seen[code/8] & (1 << (code%8)))
	goto malformed;
      if (code && code < sizeof(bgp_attr_table)/sizeof(bgp_attr_table[0]))
	{
	  struct attr_desc *desc = &bgp_attr_table[code];
	  if (desc->expected_length >= 0 && desc->expected_length != (int) l)
	    { errcode = 5; goto err; }
	  if ((desc->expected_flags ^ flags) & (BAF_OPTIONAL | BAF_TRANSITIVE))
	    { errcode = 4; goto err; }
	  if (desc->validate)
	    {
	      errcode = desc->validate(bgp, z, l);
	      if (errcode > 0)
		goto err;
	      if (errcode < 0)
		continue;
	    }
	  type = desc->type;
	}
      else				/* Unknown attribute */
	{				/* FIXME: Send partial bit when forwarding */
	  if (!(flags & BAF_OPTIONAL))
	    { errcode = 2; goto err; }
	  type = EAF_TYPE_OPAQUE;
	}
      seen[code/8] |= (1 << (code%8));
      ea = lp_alloc(pool, sizeof(ea_list) + sizeof(eattr));
      ea->next = a->eattrs;
      a->eattrs = ea;
      ea->flags = 0;
      ea->count = 1;
      ea->attrs[0].id = EA_CODE(EAP_BGP, code);
      ea->attrs[0].flags = flags;
      ea->attrs[0].type = type;
      if (type & EAF_EMBEDDED)
	ad = NULL;
      else
	{
	  ad = lp_alloc(pool, sizeof(struct adata) + l);
	  ea->attrs[0].u.ptr = ad;
	  ad->length = l;
	  memcpy(ad->data, z, l);
	}
      switch (type)
	{
	case EAF_TYPE_ROUTER_ID:
	case EAF_TYPE_INT:
	  if (l == 1)
	    ea->attrs[0].u.data = *z;
	  else
	    ea->attrs[0].u.data = get_u32(z);
	  break;
	case EAF_TYPE_IP_ADDRESS:
	  *(ip_addr *)ad->data = ipa_ntoh(*(ip_addr *)ad->data);
	  break;
	}
    }

  /* Check if all mandatory attributes are present */
  for(i=0; i < sizeof(bgp_mandatory_attrs)/sizeof(bgp_mandatory_attrs[0]); i++)
    {
      code = bgp_mandatory_attrs[i];
      if (!(seen[code/8] & (1 << (code%8))))
	{
	  bgp_error(conn, 3, 3, code, 1);
	  return NULL;
	}
    }

  /* Assign local preference if none defined */
  if (!(seen[BA_LOCAL_PREF/8] & (1 << (BA_LOCAL_PREF%8))))
    {
      ea = lp_alloc(pool, sizeof(ea_list) + sizeof(eattr));
      ea->next = a->eattrs;
      a->eattrs = ea;
      ea->flags = 0;
      ea->count = 1;
      ea->attrs[0].id = EA_CODE(EAP_BGP, BA_LOCAL_PREF);
      ea->attrs[0].flags = BAF_OPTIONAL;
      ea->attrs[0].type = EAF_TYPE_INT;
      ea->attrs[0].u.data = bgp_local_pref(bgp, a);
    }

  /* If the AS path attribute contains our AS, reject the routes */
  e = ea_find(a->eattrs, EA_CODE(EAP_BGP, BA_AS_PATH));
  ASSERT(e);
  if (bgp_path_loopy(bgp, e))
    return NULL;

  /* Fill in the remaining rta fields */
  e = ea_find(a->eattrs, EA_CODE(EAP_BGP, BA_NEXT_HOP));
  ASSERT(e);
  nexthop = *(ip_addr *) e->u.ptr->data;
  if (ipa_equal(nexthop, bgp->local_addr))
    {
      DBG("BGP: Loop!\n");		/* FIXME */
      return NULL;
    }
  neigh = neigh_find(&bgp->p, &nexthop, 0) ? : bgp->neigh;
  a->gw = neigh->addr;
  a->iface = neigh->iface;
  return rta_lookup(a);

malformed:
  bgp_error(conn, 3, 1, len, 0);
  return NULL;

err:
  bgp_error(conn, 3, errcode, code, 0);	/* FIXME: Return attribute data! */
  return NULL;
}

int
bgp_get_attr(eattr *a, byte *buf)
{
  unsigned int i = EA_ID(a->id);
  struct attr_desc *d;

  if (i && i < sizeof(bgp_attr_table)/sizeof(bgp_attr_table[0]))
    {
      d = &bgp_attr_table[i];
      buf += bsprintf(buf, "%s", d->name);
      if (d->format)
	{
	  *buf++ = ':';
	  *buf++ = ' ';
	  d->format(a, buf);
	  return GA_FULL;
	}
      return GA_NAME;
    }
  bsprintf(buf, "%02x%s", i, (a->flags & BAF_TRANSITIVE) ? "[t]" : "");
  return GA_NAME;
}

void
bgp_attr_init(struct bgp_proto *p)
{
  p->hash_size = 256;
  p->hash_limit = p->hash_size * 4;
  p->bucket_table = mb_allocz(p->p.pool, p->hash_size * sizeof(struct bgp_bucket *));
}