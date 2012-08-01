
static int
configure_ifa_add_prefix(ip_addr addr, unsigned int len,
                         u32 rid,
                         u32 my_rid,
                         int pa_priority,
                         struct ospf_iface *ifa)
{
  struct prefix_node *pxn;
  char cmd[128];
  char ip6addr[40];
  struct ospf_area *oa = ifa->oa;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;

  /* Add the prefix to the interface */
  pxn = mb_alloc(p->pool, sizeof(struct prefix_node));
  pxn->px.addr = addr;
  pxn->px.len = len;
  pxn->rid = rid;
  pxn->my_rid = my_rid;
  pxn->pa_priority = pa_priority;
  pxn->valid = 1;
  add_tail(&ifa->asp_list, NODE pxn);
  strncpy(pxn->ifname, ifa->iface->name, 16);

  /* And then configure it to the system */
  // FIXME need a better way to do this.
  // FIXME #2 BIRD seems to create a new ospf_iface struct when addresses change on an interface.
  // Maybe the interfaces' asp_list should be placed elsewhere than in the ospf_iface struct.
  // FIXME #3 This should probably be in a sysdep file.
  ip_ntop(addr, ip6addr);
  snprintf(cmd, sizeof(cmd), "ip -6 addr add %s%x:%x/%d dev %s",
           ip6addr,
           my_rid >> 16,
           my_rid & 0xFFFF,
           len, pxn->ifname);
  return system(cmd);
}

static int
configure_ifa_del_prefix(struct prefix_node *pxn)
{
  char cmd[128];
  char ip6addr[40];
  int rv;

  /* Remove the prefix from the system */
  // FIXME need a better way to do this.
  // FIXME #2 BIRD seems to create a new ospf_iface struct when addresses change on an interface.
  // Maybe the interfaces' asp_list should be placed elsewhere than in the ospf_iface struct.
  // FIXME #3 This should probably be in a sysdep file.
  ip_ntop(pxn->px.addr, ip6addr);
  snprintf(cmd, sizeof(cmd), "ip -6 addr del %s%x:%x/%d dev %s",
           ip6addr,
           pxn->my_rid >> 16,
           pxn->my_rid & 0xFFFF,
           pxn->px.len, pxn->ifname);
  rv = system(cmd);

  /* And from the internal datastructure */
  rem_node(NODE pxn);
  mb_free(pxn);


  return rv;
}

